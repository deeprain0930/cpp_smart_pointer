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

int main()
{
    deeprain::UniquePtr<Student> ptr_student_1 = deeprain::make_unique<Student>(27, "deeprain");
    ptr_student_1->Print();

    deeprain::UniquePtr<Student> ptr_student_2 = std::move(ptr_student_1);
    std::cout << std::boolalpha;
    std::cout << "p1[" << static_cast<bool>(ptr_student_1) << "], p2[" << static_cast<bool>(ptr_student_2) << "]" << std::endl;
    ptr_student_2->Print();

    ptr_student_2.get()->Print();

    return 0;
}