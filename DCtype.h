#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <optional>
#include <cassert>
#include <atomic>

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
	/// 注意：建议开启 RTTI 以确保在多模块（DLL/SO）环境下的类型识别稳定性。
	using TypeId = std::type_index;
#else
	/// @brief RTTI 禁用时，使用 void* 作为类型标识符。
	/// 警告：在多模块（DLL/SO）环境下，默认的静态变量地址策略可能导致同一类型产生不同的 ID。
	/// 建议：在跨模块场景下，请启用 RTTI 或为跨模块类型特化 CustomTypeKey。
	using TypeId = const void*;
#endif

	/// @brief 用户可通过特化此结构体来提供自定义的 TypeId 生成策略。
	/// 这在禁用 RTTI 且跨多个动态库使用时非常有用，可以提供稳定的唯一地址。
	/// 约束：CustomTypeKey<T>::get() 必须返回 DC::TypeId。
	///
	/// 例如（RTTI 禁用时）：
	/// template<> struct DC::CustomTypeKey<MyType> {
	///     static DC::TypeId get() { return &MyExportedGlobalSymbol; }
	/// };
	template<typename T>
	struct CustomTypeKey {};

	namespace detail {
		template<typename T>
		concept CustomKeyAvailable = requires {
			{ CustomTypeKey<T>::get() } -> std::same_as<TypeId>;
		};
	}

	/// @brief 获取类型 T 的标识符。
	template<typename T>
	TypeId getTypeId() {
		if constexpr (detail::CustomKeyAvailable<T>) {
			return CustomTypeKey<T>::get();
		} else {
#if DC_RTTI_ENABLED
			return typeid(T);
#else
			static const char id = 0;
			return &id;
#endif
		}
	}

	//===================================================================//
	//                      类型注册系统 (Type Registry)                   //
	//===================================================================//

	/// @brief 抽象基类，用于类型擦除，以便在全局注册表中存储不同类型的 TypeRegistry。
	struct ITypeRegistry {
		virtual ~ITypeRegistry() = default;
		[[nodiscard]] virtual std::string getEnumTypeName() const = 0;
		virtual void freeze() = 0;
		[[nodiscard]] virtual bool isFrozen() const = 0;
	};

	/// @brief 线程安全的类型到枚举的映射存储。
	/// @tparam Enum 用于映射的枚举类型。
	template<class Enum>
	class TypeRegistry final : public ITypeRegistry {
	private:
		using TypeEnumMap = std::unordered_map<TypeId, Enum>;

		mutable std::mutex mutex_;
		TypeEnumMap mappings_;
		mutable std::atomic<bool> frozen_{false};
		std::optional<Enum> fallback_;

		void ensureFrozen() const {
			if (!frozen_.load(std::memory_order_acquire)) {
				std::unique_lock lock(mutex_);
				// Check again to avoid race
				if (!frozen_.load(std::memory_order_relaxed)) {
					frozen_.store(true, std::memory_order_release);
				}
			}
		}

	public:
		void freeze() override {
			std::unique_lock lock(mutex_);
			frozen_.store(true, std::memory_order_release);
		}

		[[nodiscard]] bool isFrozen() const override {
			return frozen_.load(std::memory_order_acquire);
		}

		/// @brief 设置查询失败时返回的 fallback 值。建议在 freeze 之前设置。
		void setFallback(Enum value) {
			std::unique_lock lock(mutex_);
			assert(!frozen_.load(std::memory_order_relaxed) && "Cannot set fallback after freeze.");
			fallback_ = value;
		}

		[[nodiscard]] std::optional<Enum> tryGetFallback() const {
			if (isFrozen()) {
				return fallback_;
			}
			std::unique_lock lock(mutex_);
			return fallback_;
		}

		/// @brief 注册一个类型到指定的枚举值。
		/// @tparam T 要注册的类型。
		/// @param value 与类型 T 关联的枚举值。
		/// @return 若成功注册返回 true；若已冻结则返回 false。
		template<class T>
		bool registerType(Enum value) {

			if (frozen_.load(std::memory_order_acquire)) {
				return false;
			}
			std::unique_lock lock(mutex_);

			if (frozen_.load(std::memory_order_relaxed)) {
				return false;
			}
			mappings_[getTypeId<T>()] = value;
			return true;
		}

		/// @brief 查询类型 T 对应的枚举值。
		/// @tparam T 要查询的类型。
		/// @return 如果找到，则返回对应的枚举值；否则返回 fallback（若已设置），否则返回枚举的默认构造值。
		template<class T>
		[[nodiscard]] Enum getType() const {
			ensureFrozen();

			// Lock-free read
			auto it = mappings_.find(getTypeId<T>());
			if (it != mappings_.end()) {
				return it->second;
			}

			if (fallback_.has_value()) {
				return *fallback_;
			}

			return Enum{};
		}

		/// @brief 查询类型 T 对应的枚举值，如果未找到则返回提供的备用值。
		/// @tparam T 要查询的类型。
		/// @param fallback 如果未找到类型 T 的映射，则返回此值。
		/// @return 如果找到，则返回对应的枚举值；否则返回 fallback。
		template<class T>
		[[nodiscard]] Enum getTypeOr(Enum fallback) const {
			ensureFrozen();

			auto it = mappings_.find(getTypeId<T>());
			return it != mappings_.end() ? it->second : fallback;
		}

		/// @brief 尝试查询类型 T 对应的枚举值。
		/// @tparam T 要查询的类型。
		/// @return std::optional 包含枚举值，如果未找到则为 std::nullopt。
		template<class T>
		[[nodiscard]] std::optional<Enum> tryGetType() const {
			ensureFrozen();

			auto it = mappings_.find(getTypeId<T>());
			return it != mappings_.end() ? std::optional<Enum>(it->second) : std::nullopt;
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

		std::shared_mutex mutex_;
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
			const auto key = getTypeId<Enum>();

			// 尝试读取锁查找
			{
				std::shared_lock lock(mutex_);
				auto it = registries_.find(key);
				if (it != registries_.end()) {
					return *static_cast<TypeRegistry<Enum>*>(it->second.get());
				}
			}

			// 获取写入锁并再次检查 (Double-checked locking)
			std::unique_lock lock(mutex_);
			auto it = registries_.find(key);
			if (it != registries_.end()) {
				return *static_cast<TypeRegistry<Enum>*>(it->second.get());
			}

			auto registry = std::make_unique<TypeRegistry<Enum>>();
			auto* ptr = registry.get();
			registries_[key] = std::move(registry);
			return *ptr;
		}

		/// @brief 冻结指定枚举类型对应的注册表。
		template<class Enum>
		void freeze() {
			getRegistry<Enum>().freeze();
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
	bool registerType(Enum value) {
		return GlobalRegistry::instance().getRegistry<Enum>().template registerType<T>(value);
	}

	/// @brief 为某个枚举注册表设置查询失败时返回的 fallback 值。
	template<class Enum>
	void setFallback(Enum fallback) {
		GlobalRegistry::instance().getRegistry<Enum>().setFallback(fallback);
	}

	/// @brief 冻结某个枚举的注册表。冻结后禁止注册，并允许安全查询。
	template<class Enum>
	void freeze() {
		GlobalRegistry::instance().freeze<Enum>();
	}

	/// @brief 查询与给定实例类型关联的枚举值。
	/// @tparam Enum 目标枚举类型。
	/// @tparam T 实例的类型。
	/// @return 如果找到，则返回对应的枚举值；否则返回 fallback（若已设置）或默认构造值。
	template<class Enum, class T>
	[[nodiscard]] Enum getType(const T&) {
		return GlobalRegistry::instance().getRegistry<Enum>().template getType<T>();
	}

	/// @brief 查询与类型 T 关联的枚举值。
	/// @tparam Enum 目标枚举类型。
	/// @tparam T 要查询的类型。
	/// @return 如果找到，则返回对应的枚举值；否则返回 fallback（若已设置）或默认构造值。
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

	/// @brief 尝试查询与给定实例类型关联的枚举值。
	/// @tparam Enum 目标枚举类型。
	/// @tparam T 实例的类型。
	/// @return std::optional 包含枚举值，如果未找到则为 std::nullopt。
	template<class Enum, class T>
	[[nodiscard]] std::optional<Enum> tryGetType(const T&) {
		return GlobalRegistry::instance().getRegistry<Enum>().template tryGetType<T>();
	}

	/// @brief 尝试查询与类型 T 关联的枚举值。
	/// @tparam Enum 目标枚举类型。
	/// @tparam T 要查询的类型。
	/// @return std::optional 包含枚举值，如果未找到则为 std::nullopt。
	template<class Enum, class T>
	[[nodiscard]] std::optional<Enum> tryGetType() {
		return GlobalRegistry::instance().getRegistry<Enum>().template tryGetType<T>();
	}

} // namespace DC