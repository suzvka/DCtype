#include "DCtype.h"


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

int main() {
    using namespace DC;
    using ShapeDomain = DomainTag<Shape, ShapeKind>;

    // 注册类型到 ShapeKind 域
    registerInDomain<ShapeDomain, Circle>(ShapeKind::Circle);
    registerInDomain<ShapeDomain, Square>(ShapeKind::Square);
    registerInDomain<ShapeDomain, Triangle>(ShapeKind::Triangle);

    // 冻结所有域
    freezeAllDomains();

    // 测试查询
    std::unique_ptr<Shape> shape1 = std::make_unique<Circle>();
    std::unique_ptr<Shape> shape2 = std::make_unique<Square>();
    std::unique_ptr<Shape> shape3 = std::make_unique<Triangle>();

    auto kind1 = getFromDomain<ShapeDomain>(*shape1);
    auto kind2 = getFromDomain<ShapeDomain>(*shape2);
    auto kind3 = getFromDomain<ShapeDomain>(*shape3);

    std::cout << "Circle -> " << static_cast<int>(kind1) << std::endl;
    std::cout << "Square -> " << static_cast<int>(kind2) << std::endl;
    std::cout << "Triangle -> " << static_cast<int>(kind3) << std::endl;

    return 0;
}