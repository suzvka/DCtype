#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>

// 检测 RTTI 是否启用
#if defined(__GXX_RTTI) || defined(_CPPRTTI)
    #define DC_RTTI_ENABLED 1
#else
    #define DC_RTTI_ENABLED 0
#endif

namespace DC {

    //===================================================================//
    //                  类型标识符 (Type Identifier)                     //
    //===================================================================//

#if DC_RTTI_ENABLED
    /// @brief RTTI 启用时，使用 std::type_index 作为类型标识符。
    using TypeId = std::type_index;

    /// @brief 获取类型 T 的标识符。
    template<typename T>
    TypeId getTypeId() {
        return typeid(T);
    }
#else
    /// @brief RTTI 禁用时，使用 void* 作为类型标识符。
    using TypeId = const void*;

    /// @brief 获取类型 T 的标识符。
    /// 每个模板实例化都会生成一个唯一的静态变量，其地址可作为该类型的唯一标识符。
    template<typename T>
    TypeId getTypeId() {
        static const char id = 0;
        return &id;
    }
#endif

    //===================================================================//
    //                      类型注册系统 (Type Registry)                   //
    //===================================================================//

    /// @brief 抽象基类，用于类型擦除，以便在全局注册表中存储不同类型的 TypeRegistry。
    struct ITypeRegistry {
        virtual ~ITypeRegistry() = default;
        [[nodiscard]] virtual std::string getEnumTypeName() const = 0;
    };

    /// @brief 线程安全的类型到枚举的映射存储。
    /// @tparam Enum 用于映射的枚举类型。
    template<class Enum>
    class TypeRegistry final : public ITypeRegistry {
    private:
        using TypeEnumMap = std::unordered_map<TypeId, Enum>;

        mutable std::mutex mutex_;
        TypeEnumMap mappings_;

    public:
        /// @brief 注册一个类型到指定的枚举值。
        /// @tparam T 要注册的类型。
        /// @param value 与类型 T 关联的枚举值。
        template<class T>
        void registerType(Enum value) {
            std::scoped_lock lock(mutex_);
            mappings_[getTypeId<T>()] = value;
        }

        /// @brief 查询类型 T 对应的枚举值。
        /// @tparam T 要查询的类型。
        /// @return 如果找到，则返回对应的枚举值；否则返回枚举的默认构造值。
        template<class T>
        [[nodiscard]] Enum getType() const {
            std::scoped_lock lock(mutex_);
            auto it = mappings_.find(getTypeId<T>());
            return it != mappings_.end() ? it->second : Enum{};
        }

        /// @brief 查询类型 T 对应的枚举值，如果未找到则返回提供的备用值。
        /// @tparam T 要查询的类型。
        /// @param fallback 如果未找到类型 T 的映射，则返回此值。
        /// @return 如果找到，则返回对应的枚举值；否则返回 fallback。
        template<class T>
        [[nodiscard]] Enum getTypeOr(Enum fallback) const {
            std::scoped_lock lock(mutex_);
            auto it = mappings_.find(getTypeId<T>());
            return it != mappings_.end() ? it->second : fallback;
        }

        /// @brief 获取此注册表管理的枚举类型的名称。
        [[nodiscard]] std::string getEnumTypeName() const override {
#if DC_RTTI_ENABLED
            return typeid(Enum).name();
#else
            return "Unknown (RTTI disabled)";
#endif
        }
    };

    /// @brief 全局注册表管理器，作为所有 TypeRegistry 实例的中心存储。
    class GlobalRegistry {
    private:
        using RegistryMap = std::unordered_map<TypeId, std::unique_ptr<ITypeRegistry>>;

        std::mutex mutex_;
        RegistryMap registries_;

        GlobalRegistry() = default;

    public:
        GlobalRegistry(const GlobalRegistry&) = delete;
        GlobalRegistry& operator=(const GlobalRegistry&) = delete;

        /// @brief 获取 GlobalRegistry 的单例实例。
        static GlobalRegistry& instance() {
            static GlobalRegistry inst;
            return inst;
        }

        /// @brief 获取或创建指定枚举类型的 TypeRegistry。
        /// @tparam Enum 注册表所管理的枚举类型。
        /// @return 对 TypeRegistry 实例的引用。
        template<class Enum>
        TypeRegistry<Enum>& getRegistry() {
            std::scoped_lock lock(mutex_);
            const auto key = getTypeId<Enum>();
            auto it = registries_.find(key);

            if (it == registries_.end()) {
                auto registry = std::make_unique<TypeRegistry<Enum>>();
                auto* ptr = registry.get();
                registries_[key] = std::move(registry);
                return *ptr;
            }

            return *static_cast<TypeRegistry<Enum>*>(it->second.get());
        }
    };

    //===================================================================//
    //                         公共 API (Public API)                       //
    //===================================================================//

    /// @brief 注册一个类型到指定的枚举值。
    /// @tparam T 要注册的类型。
    /// @tparam Enum 目标枚举类型。
    /// @param value 与类型 T 关联的枚举值。
    template<class T, class Enum>
    void registerType(Enum value) {
        GlobalRegistry::instance().getRegistry<Enum>().template registerType<T>(value);
    }

    /// @brief 查询与给定实例类型关联的枚举值。
    /// @tparam Enum 目标枚举类型。
    /// @tparam T 实例的类型。
    /// @return 如果找到，则返回对应的枚举值；否则返回枚举的默认构造值。
    template<class Enum, class T>
    [[nodiscard]] Enum getType(const T&) {
        return GlobalRegistry::instance().getRegistry<Enum>().template getType<T>();
    }

    /// @brief 查询与类型 T 关联的枚举值。
    /// @tparam Enum 目标枚举类型。
    /// @tparam T 要查询的类型。
    /// @return 如果找到，则返回对应的枚举值；否则返回枚举的默认构造值。
    template<class Enum, class T>
    [[nodiscard]] Enum getType() {
        return GlobalRegistry::instance().getRegistry<Enum>().template getType<T>();
    }

    /// @brief 查询与给定实例类型关联的枚举值，如果未找到则返回备用值。
    /// @tparam Enum 目标枚举类型。
    /// @tparam T 实例的类型。
    /// @param fallback 如果未找到映射，则返回此值。
    /// @return 如果找到，则返回对应的枚举值；否则返回 fallback。
    template<class Enum, class T>
    [[nodiscard]] Enum getTypeOr(const T&, Enum fallback) {
        return GlobalRegistry::instance().getRegistry<Enum>().template getTypeOr<T>(fallback);
    }

    /// @brief 查询与类型 T 关联的枚举值，如果未找到则返回备用值。
    /// @tparam Enum 目标枚举类型。
    /// @tparam T 要查询的类型。
    /// @param fallback 如果未找到映射，则返回此值。
    /// @return 如果找到，则返回对应的枚举值；否则返回 fallback。
    template<class Enum, class T>
    [[nodiscard]] Enum getTypeOr(Enum fallback) {
        return GlobalRegistry::instance().getRegistry<Enum>().template getTypeOr<T>(fallback);
    }

} // namespace DC