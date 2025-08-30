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
	ValueC
};

void simplified_test() {
    std::cout << "Testing simplified API..." << std::endl;

    // 注册类型
    DC::registerType<std::string, MyEnum>(MyEnum::ValueC);
    DC::registerType<int, MyEnum>(MyEnum::ValueA);
    DC::registerType<double, MyEnum>(MyEnum::ValueB);

    // 查询方式1：传入实例
    std::string str = "hello";
    int num = 42;
    double pi = 3.14;
    
    assert(DC::getType<MyEnum>(str) == MyEnum::ValueC);
    assert(DC::getType<MyEnum>(num) == MyEnum::ValueA);
    assert(DC::getType<MyEnum>(pi) == MyEnum::ValueB);

    // 查询方式2：不需要实例
    ((void)((!!(DC::getType<MyEnum, std::string>() == MyEnum::ValueC)) || (_wassert(L"DC::getType<MyEnum", L"E:\\DClib\\DCtype\\Test.cpp", (unsigned)(64)), 0)));
    ((void)((!!(DC::getType<MyEnum, int>() == MyEnum::ValueA)) || (_wassert(L"DC::getType<MyEnum", L"E:\\DClib\\DCtype\\Test.cpp", (unsigned)(65)), 0)));
    ((void)((!!(DC::getType<MyEnum, double>() == MyEnum::ValueB)) || (_wassert(L"DC::getType<MyEnum", L"E:\\DClib\\DCtype\\Test.cpp", (unsigned)(66)), 0)));

    // 测试未注册类型
    ((void)((!!(DC::getTypeOr<MyEnum, float>(MyEnum::ValueA) == MyEnum::ValueA)) || (_wassert(L"DC::getTypeOr<MyEnum", L"E:\\DClib\\DCtype\\Test.cpp", (unsigned)(69)), 0)));

    std::cout << "Simplified API tests passed" << std::endl;
}

int main() {
	simplified_test();
	return 0;
}