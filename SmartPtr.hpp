#include <type_traits>
#include <iostream>
#include <typeinfo>

#define DEEPRAIN_DEBUG_ 1

namespace deeprain {
    template <typename T>
    struct DefaultDeleter { // 默认删除器
        void operator()(T* ptr) {
            delete ptr;
        }
    };

    template <typename T>
    struct DefaultDeleter<T[]> { // 数组特化
        void operator()(T* ptr) {
            delete[] ptr;
        }
    };

    template <typename T, typename Deleter = DefaultDeleter<T>>
    struct UniquePtr {
        using Tp = T;
    private:
        T* ptr_;
        Deleter deleter_;
    public:
        UniquePtr(std::nullptr_t = nullptr) noexcept : ptr_(nullptr) {
            #ifdef DEEPRAIN_DEBUG_
            std::cout << "default construct[" <<  typeid(T).name() << "]" << std::endl;
            #endif
        };

        UniquePtr(T* ptr) noexcept : ptr_(ptr) {
            #ifdef DEEPRAIN_DEBUG_
            std::cout << "construct[" <<  typeid(T).name() << "]" << std::endl;
            #endif
        };

        template <typename U_, typename UDeleter_>
        requires std::is_convertible_v<U_*, T*>
        UniquePtr(UniquePtr<U_, UDeleter_>&& that) noexcept : ptr_(that.ptr_), deleter_(that.deleter_) {
            #ifdef DEEPRAIN_DEBUG_
            std::cout << "conversion move construct[" <<  typeid(T).name() << "]" << std::endl;
            #endif

            that.ptr_ = nullptr;
        };

        UniquePtr(UniquePtr&& that) noexcept : ptr_(that.ptr_), deleter_(that.deleter_) {
            #ifdef DEEPRAIN_DEBUG_
            std::cout << "move construct[" <<  typeid(T).name() << "]" << std::endl;
            #endif

            that.ptr_ = nullptr;
        }

        template <typename _U, typename _UDeleter>
        requires std::is_convertible_v<_U, T>
        UniquePtr& operator=(UniquePtr<_U, _UDeleter>&& that) noexcept {
            #ifdef DEEPRAIN_DEBUG_
            std::cout << "move construct[" <<  typeid(T).name() << "]" << std::endl;
            #endif
 
            if(&that == this)
                return *this;
            ptr_ = that.ptr_;
            deleter_ = that.deleter_;
            that.ptr_ = nullptr;                

            return *this;
        } 

        UniquePtr& operator=(UniquePtr&& that) noexcept {
            #ifdef DEEPRAIN_DEBUG_
            std::cout << "move assignment[" <<  typeid(T).name() << "]" << std::endl;
            #endif

            if(&that == this)
                return *this;
            ptr_ = that.ptr_;
            deleter_ = that.deleter_;
            that.ptr_ = nullptr;

            return *this;
        }; 

        ~UniquePtr() {
            #ifdef DEEPRAIN_DEBUG_
            std::cout << "destruction[" <<  typeid(T).name() << "]" << std::endl;
            #endif

            if(ptr_ != nullptr)
                deleter_(ptr_);
        }

        UniquePtr(const UniquePtr&) = delete;
        UniquePtr& operator=(const UniquePtr&) = delete;

        // 返回指针并释放所有权
        T* release() noexcept {
            T* ret_ptr = ptr_;
            ptr_ = nullptr; 

            return ptr_;
        }

        // 只返回指针不弃置所有权
        T* get() noexcept {
            return ptr_;
        }

        // 重置
        void reset(T* that = nullptr) {
            if(this == that) 
                return;

            if(ptr_ != nullptr)
                deleter_(ptr_);

            ptr_ = nullptr;
        }

        // 获取删除器
        const Deleter& get_deleter() {
            return deleter_;
        }

        // 解引用
        // add_lvalue_reference可以仅实例化支持引用的，例如void就不支持
        std::add_lvalue_reference_t<T>& operator*() noexcept {
            return *ptr_;
        }

        // 指针操作
        T* operator->() {
            return ptr_;
        }

        // 交换
        void swap(UniquePtr& that) noexcept {
            std::swap(that.ptr_, ptr_);
        }

        // 指针是否有效
        operator bool()
        {
            return ptr_ != nullptr;
        }
    };

    template <typename T, class Deleter>
    struct UniquePtr<T[], Deleter> : public UniquePtr<T, Deleter> {
        std::add_lvalue_reference_t<T> operator[](std::size_t idx) {
            return this->get()[idx];
        }
    };

    template <typename T, typename ...Args>
    requires (!std::is_unbounded_array_v<T>)
    UniquePtr<T> make_unique(Args&& ...args) {
        return UniquePtr<T>(new T(std::forward<Args>(args)...));
    }
    
    template <typename T>
    requires (!std::is_unbounded_array_v<T>)
    UniquePtr<T> make_unique() {
        return UniquePtr<T>(new T());
    }

    template <typename T, typename ...Args>
    requires (std::is_unbounded_array_v<T>)
    UniquePtr<T> make_unique(std::size_t len, Args&& ...args) {
        return UniquePtr<T>(new std::remove_extent_t<T>[len](std::forward<Args>(args)...));
    }

    template <typename T>
    requires (std::is_unbounded_array_v<T>)
    UniquePtr<T> make_unique(std::size_t len) {
        return UniquePtr<T>(new std::remove_extent_t<T>[len]()); 
    }

    struct SharedPtr {

    };
}