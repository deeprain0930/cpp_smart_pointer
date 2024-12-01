#include "SmartPtr.hpp"

#include <ios>
#include <memory>
#include <iostream>
#include <string>

#define DEEPRAIN_DEBUG_ 1

struct Student {
    int age;
    std::string name;

    Student() : age(0), name("") {};

    Student(const int age, const std::string& name) : age(age), name(name) {};

    void Print() {
        std:: cout << "student age[" << age << "] ,name[" << name << "]" << std::endl;
    }
};

struct alignas(Student) TestClass{};

int main()
{
    deeprain::UniquePtr<Student> ptr_student_1 = deeprain::make_unique<Student>(27, "deeprain");
    deeprain::SharedPtr<Student> sp2 = deeprain::make_shared<Student>();

    return 0;
}