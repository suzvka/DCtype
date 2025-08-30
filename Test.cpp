#include "DCtype.h"


struct Shape {
    virtual ~Shape() = default; // ����������������
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

// ����һ����ص�ö��
enum class ShapeKind {
    Circle,
    Square,
    Triangle,
    GenericPolygon
};

// ��һ����ȫ��ͬ��ö�٣����ڲ�ͬ��Ŀ��
enum class RenderBackend {
    OpenGL,
    Vulkan,
    Metal
};

int main() {
    using namespace DC;
    using ShapeDomain = DomainTag<Shape, ShapeKind>;

    // ע�����͵� ShapeKind ��
    registerInDomain<ShapeDomain, Circle>(ShapeKind::Circle);
    registerInDomain<ShapeDomain, Square>(ShapeKind::Square);
    registerInDomain<ShapeDomain, Triangle>(ShapeKind::Triangle);

    // ����������
    freezeAllDomains();

    // ���Բ�ѯ
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