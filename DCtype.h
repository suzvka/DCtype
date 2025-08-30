#pragma once
#include <type_traits>
#include <typeindex>
#include <cstdint>
#include <utility>
#include <mutex>
#include <vector>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <iostream>
#include <algorithm>

namespace DC {
    // Forward declarations
    struct TypeKey;
    struct IAnyDomain;
    struct Diagnostics;
    class Registry;
    template<class Enum> class FrozenMap;
    template<class Enum> class MutableBuilder;
    template<class Base, class Enum, class KeyStrat> class DomainStorage;

    //===================================================================//
    // Type Key Infrastructure and Strategy Pattern                     //
    //===================================================================//

    /// @brief Unique identifier for a dynamic type within a process
    /// Provides stable identification for type-to-enum mapping
    struct TypeKey {
        std::uintptr_t value{};

        friend bool operator==(TypeKey a, TypeKey b) noexcept {
            return a.value == b.value;
        }
        friend bool operator!=(TypeKey a, TypeKey b) noexcept {
            return !(a == b);
        }
    };

    /// @brief RTTI-based key generation strategy
    /// Uses std::type_index for type identification
    struct RTTIKeyStrategy {
        template<class T>
        static TypeKey keyOfStatic() noexcept {
            return TypeKey{ static_cast<std::uintptr_t>(std::type_index(typeid(T)).hash_code()) };
        }

        template<class Base>
        static TypeKey keyOfDynamic(const Base& obj) noexcept {
            return TypeKey{ static_cast<std::uintptr_t>(std::type_index(typeid(obj)).hash_code()) };
        }
    };

    /// @brief Interface for custom anchor-based type identification
    struct IAnchorProvider {
        virtual TypeKey getTypeAnchor() const noexcept = 0;
    };

    /// @brief Custom anchor-based key generation strategy
    /// Uses ODR-unique addresses for type identification
    struct AnchorKeyStrategy {
        template<class T>
        static TypeKey keyOfStatic() noexcept {
            static int anchor;
            return TypeKey{ reinterpret_cast<std::uintptr_t>(&anchor) };
        }

        template<class Base>
        static TypeKey keyOfDynamic(const Base& obj) noexcept {
            if constexpr (std::is_base_of_v<IAnchorProvider, Base>) {
                return obj.getTypeAnchor();
            }
            else {
                return keyOfStatic<std::decay_t<decltype(obj)>>();
            }
        }
    };

    /// @brief Concept defining key generation strategy requirements
    template<class S, class B, class D>
    concept KeyStrategy = requires(const B & b) {
        { S::template keyOfStatic<D>() } -> std::same_as<TypeKey>;
        { S::template keyOfDynamic<B>(b) } -> std::same_as<TypeKey>;
    };

    //===================================================================//
    // Domain Modeling                                                  //
    //===================================================================//

    /// @brief Type-based domain identifier
    /// Encapsulates base type and enum type for a specific domain
    template<class Base, class Enum>
    struct DomainTag {
        using base_type = Base;
        using enum_type = Enum;
    };

    /// @brief Runtime domain identifier for registry indexing
    struct DomainId {
        std::uintptr_t value{};

        template<class Base, class Enum>
        static constexpr DomainId make() noexcept {
            auto h1 = std::hash<std::type_index>{}(std::type_index(typeid(Base)));
            auto h2 = std::hash<std::type_index>{}(std::type_index(typeid(Enum)));
            return DomainId{ h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2)) };
        }

        friend bool operator==(DomainId a, DomainId b) noexcept {
            return a.value == b.value;
        }
    };

    //===================================================================//
    // Type Traits and Concepts                                         //
    //===================================================================//

    /// @brief Trait to deduce base type from derived type and enum
    template<class Derived, class Enum>
    struct primary_base_for {
        // Implementation provided by specialization
    };

    /// @brief Detects if type has explicit base_type member
    template<class T, class = void>
    struct has_base_type : std::false_type {};

    template<class T>
    struct has_base_type<T, std::void_t<typename T::base_type>> : std::true_type {};

    //===================================================================//
    // Storage Implementation                                            //
    //===================================================================//

    /// @brief Immutable, thread-safe mapping from TypeKey to Enum
    /// Optimized for fast lookups after construction
    template<class Enum>
    class FrozenMap {
    private:
        std::vector<std::pair<TypeKey, Enum>> entries_;
        bool is_sorted_{ false };

    public:
        /// @brief Lookup with fallback value
        Enum getOr(Enum fallback, TypeKey key) const noexcept {
            if (entries_.empty()) return fallback;

            auto it = std::lower_bound(entries_.begin(), entries_.end(),
                std::make_pair(key, Enum{}),
                [](const auto& a, const auto& b) {
                    return a.first.value < b.first.value;
                });

            if (it != entries_.end() && it->first == key) {
                return it->second;
            }
            return fallback;
        }

        /// @brief Create frozen map from sorted data
        static FrozenMap from(std::vector<std::pair<TypeKey, Enum>>&& data) {
            FrozenMap map;
            map.entries_ = std::move(data);
            std::sort(map.entries_.begin(), map.entries_.end(),
                [](const auto& a, const auto& b) {
                    return a.first.value < b.first.value;
                });
            map.is_sorted_ = true;
            return map;
        }
    };

    /// @brief Mutable builder for constructing FrozenMap
    /// Used during registration phase
    template<class Enum>
    class MutableBuilder {
    private:
        std::vector<std::pair<TypeKey, Enum>> entries_;

    public:
        /// @brief Insert or update mapping
        bool insert(TypeKey k, Enum v) {
            auto it = std::find_if(entries_.begin(), entries_.end(),
                [k](const auto& pair) { return pair.first == k; });

            if (it != entries_.end()) {
                Diagnostics::onDuplicateRegistration("", k);
                it->second = v;
                return true;
            }

            entries_.emplace_back(k, v);
            return true;
        }

        /// @brief Convert to immutable frozen map
        FrozenMap<Enum> freeze() {
            return FrozenMap<Enum>::from(std::move(entries_));
        }
    };

    //===================================================================//
    // Domain Storage                                                    //
    //===================================================================//

    /// @brief Thread-safe storage for a single domain's type mappings
    /// Supports registration phase and frozen lookup phase
    template<class Base, class Enum, class KeyStrat = RTTIKeyStrategy>
    class DomainStorage {
    public:
        using base_type = Base;
        using enum_type = Enum;
        using strategy = KeyStrat;

        /// @brief Register type-to-enum mapping
        template<class Derived>
        bool registerType(Enum value) {
            static_assert(std::is_base_of_v<Base, Derived>, "Derived must inherit from Base");
            auto key = KeyStrat::template keyOfStatic<Derived>();
            std::scoped_lock lk(mu_);
            if (frozen_) return false;
            return builder_.insert(key, value);
        }

        /// @brief Freeze storage for optimized lookups
        void freeze() {
            std::scoped_lock lk(mu_);
            if (frozen_) return;
            frozen_map_ = std::move(builder_).freeze();
            frozen_ = true;
        }

        /// @brief Query enum value for object's dynamic type
        Enum queryOr(Enum fallback, const Base& obj) const noexcept {
            auto key = KeyStrat::template keyOfDynamic<Base>(obj);
            return frozen_map_.getOr(fallback, key);
        }

    private:
        mutable std::mutex mu_;
        bool frozen_{ false };
        MutableBuilder<Enum> builder_;
        FrozenMap<Enum> frozen_map_;
    };

    //===================================================================//
    // Registry System                                                   //
    //===================================================================//

    /// @brief Type-erased domain interface for management
    struct IAnyDomain {
        virtual ~IAnyDomain() = default;
        virtual void freeze() = 0;
    };

    /// @brief Concrete domain wrapper implementing IAnyDomain
    template<class Base, class Enum, class KeyStrat = RTTIKeyStrategy>
    class DomainHandle final : public IAnyDomain {
    public:
        using Storage = DomainStorage<Base, Enum, KeyStrat>;

        Storage& storage() noexcept { return storage_; }
        void freeze() override { storage_.freeze(); }

    private:
        Storage storage_;
    };

    /// @brief Global registry managing all domain instances
    class Registry {
    public:
        /// @brief Get singleton instance
        static Registry& instance() {
            static Registry inst;
            return inst;
        }

        /// @brief Get or create domain handle
        template<class Base, class Enum, class KeyStrat = RTTIKeyStrategy>
        DomainHandle<Base, Enum, KeyStrat>& domain() {
            const auto id = DomainId::make<Base, Enum>();
            std::scoped_lock lk(mu_);

            auto it = domains_.find(id.value);
            if (it == domains_.end()) {
                auto handle = std::make_unique<DomainHandle<Base, Enum, KeyStrat>>();
                auto* ptr = handle.get();
                domains_[id.value] = std::move(handle);
                return *ptr;
            }

            auto* handle = static_cast<DomainHandle<Base, Enum, KeyStrat>*>(it->second.get());
            return *handle;
        }

        /// @brief Freeze all registered domains
        void freezeAll() {
            std::scoped_lock lk(mu_);
            for (auto& [id, domain] : domains_) {
                domain->freeze();
            }
        }

    private:
        std::mutex mu_;
        std::unordered_map<std::uintptr_t, std::unique_ptr<IAnyDomain>> domains_;
    };

    //===================================================================//
    // Public API                                                        //
    //===================================================================//

    /// @brief Register type with explicit base, enum, and derived types
    template<class Base, class Enum, class Derived, class KeyStrat = RTTIKeyStrategy>
    void registerType(Enum value) {
        static_assert(std::is_base_of_v<Base, Derived>, "Derived must inherit from Base");
        auto& h = Registry::instance().template domain<Base, Enum, KeyStrat>();
        h.storage().template registerType<Derived>(value);
    }

    /// @brief Register type with base type deduction
    template<class Derived, class Enum, class KeyStrat = RTTIKeyStrategy>
    void registerType(Enum value) {
        using Base = std::conditional_t<has_base_type<Derived>::value,
            typename Derived::base_type,
            typename primary_base_for<Derived, Enum>::type>;
        registerType<Base, Enum, Derived, KeyStrat>(value);
    }

    /// @brief Batch register type to multiple domains
    template<class Derived, class... Args>
    void registerTypeToMany(Args&&... args) {
        auto register_one = [](auto&& pair) {
            using PairType = std::decay_t<decltype(pair)>;
            using EnumType = typename PairType::second_type;
            registerType<Derived, EnumType>(pair.second);
            };

        (register_one(std::forward<Args>(args)), ...);
    }

    /// @brief Query enum value with fallback
    template<class Base, class Enum, class KeyStrat = RTTIKeyStrategy>
    Enum getTypeOr(const Base& obj, Enum fallback) {
        auto& h = Registry::instance().template domain<Base, Enum, KeyStrat>();
        return h.storage().queryOr(fallback, obj);
    }

    /// @brief Query enum value with default fallback
    template<class Base, class Enum, class KeyStrat = RTTIKeyStrategy>
    Enum getType(const Base& obj) {
        return getTypeOr<Base, Enum, KeyStrat>(obj, Enum{});
    }

    /// @brief Freeze all domains for optimized lookups
    inline void freezeAllDomains() {
        Registry::instance().freezeAll();
    }

    //===================================================================//
    // Diagnostics and Error Handling                                   //
    //===================================================================//

    /// @brief Diagnostic hooks for error handling and logging
    struct Diagnostics {
        static void onDuplicateRegistration(std::string_view domain, TypeKey k) {
            std::cerr << "Warning: Duplicate registration in domain '" << domain
                << "' for type key " << k.value << std::endl;
        }

        static void onMiss(std::string_view domain, TypeKey k) {
            std::cerr << "Miss: Type key " << k.value
                << " not found in domain '" << domain << "'\n";
        }

        static void onFreezeAfterQuery(std::string_view domain) {
            std::cerr << "Error: Attempting to freeze domain '" << domain
                << "' after queries have begun\n";
        }
    };

    //===================================================================//
    // Domain Tag Convenience API                                       //
    //===================================================================//

    /// @brief Register type using domain tag syntax
    template<class Domain, class Derived, class KeyStrat = RTTIKeyStrategy>
    void registerInDomain(typename Domain::enum_type value) {
        using Base = typename Domain::base_type;
        using Enum = typename Domain::enum_type;
        registerType<Base, Enum, Derived, KeyStrat>(value);
    }

    /// @brief Query type using domain tag syntax
    template<class Domain, class KeyStrat = RTTIKeyStrategy>
    auto getFromDomain(const typename Domain::base_type& obj) -> typename Domain::enum_type {
        using Base = typename Domain::base_type;
        using Enum = typename Domain::enum_type;
        return getType<Base, Enum, KeyStrat>(obj);
    }
}