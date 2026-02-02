#include "DCtype.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <cassert>

struct Shape {
	virtual ~Shape() = default; // 必须有虚析构函数
	virtual std::string name() const = 0;
};

struct Circle : public Shape {
	std::string name() const override { return "Circle"; }
};

struct Square : public Shape {
	std::string name() const override { return "Square"; }
};

struct Triangle : public Shape {
	std::string name() const override { return "Triangle"; }
};

// 定义一个相关的枚举
enum class ShapeKind {
	Circle,
	Square,
	Triangle,
	GenericPolygon
};

// 另一个完全不同的枚举，用于不同的目的
enum class RenderBackend {
	OpenGL,
	Vulkan,
	Metal
};

// ========== 单元测试开始 ==========
enum class MyEnum {
	ValueA,
	ValueB,
	ValueC,
	Unknown
};

void simplified_test() {
    std::cout << "Testing simplified API..." << std::endl;

    // 注册类型
    DC::registerType<std::string, MyEnum>(MyEnum::ValueC);
    DC::registerType<int, MyEnum>(MyEnum::ValueA);
    DC::registerType<double, MyEnum>(MyEnum::ValueB);

    // 配置未注册类型的返回值
    DC::setFallback<MyEnum>(MyEnum::Unknown);
    // REMOVED explicit freeze to test auto-freeze
    // DC::freeze<MyEnum>();

    // 查询方式1：传入实例
    std::string str = "hello";
    int num = 42;
    double pi = 3.14;

    // First query should trigger auto-freeze
    assert((DC::getType<MyEnum>(str) == MyEnum::ValueC));
    assert((DC::getType<MyEnum>(num) == MyEnum::ValueA));
    assert((DC::getType<MyEnum>(pi) == MyEnum::ValueB));

    // Verify it is indeed frozen
    assert(DC::GlobalRegistry::instance().getRegistry<MyEnum>().isFrozen());
    
    // Verify registration fails after auto-freeze
    bool regResult = DC::registerType<float, MyEnum>(MyEnum::ValueA);
    assert(regResult == false);

    // 查询方式2：不需要实例
    assert((DC::getType<MyEnum, std::string>() == MyEnum::ValueC));
    assert((DC::getType<MyEnum, int>() == MyEnum::ValueA));
    assert((DC::getType<MyEnum, double>() == MyEnum::ValueB));

    // 测试未注册类型：getTypeOr 仍可覆盖 fallback
    assert((DC::getTypeOr<MyEnum, float>(MyEnum::ValueA) == MyEnum::ValueA));

    // 测试未注册类型：getType 使用注册表 fallback
    assert((DC::getType<MyEnum, float>() == MyEnum::Unknown));

    // 测试 tryGetType
    auto opt1 = DC::tryGetType<MyEnum, std::string>();
    assert(opt1.has_value() && opt1.value() == MyEnum::ValueC);

    auto opt2 = DC::tryGetType<MyEnum, float>();
    assert(!opt2.has_value());

    std::cout << "Simplified API tests passed" << std::endl;
}

int main() {
	simplified_test();
	return 0;
}