#include <memory>
#include <type_traits>
#include <iostream>
#include <typeinfo>
#include <atomic>

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

    template<typename T> 
    struct CompressedPairElement {
        T elem_;
    };

    // shared内存块，仅有shared共享计数
    struct __ControlBlock {
        std::atomic<long> shared_owners_;       // shared共享计数

        __ControlBlock(long ref = 0) : shared_owners_(ref) {}

        // 新增引用计数
        void add_shared() {
            shared_owners_.fetch_add(1, std::memory_order_relaxed);
        }

        // 减少共享计数
        bool release_shared() {
            if(shared_owners_.fetch_sub(1, std::memory_order_relaxed) == 0) {
                on_zero_shared();

                return true;
            }

            return false;
        }

        // 返回引用计数
        long use_count()
        {
            return shared_owners_.load(std::memory_order_relaxed);
        }

        // 引用计数清零调用
        virtual void on_zero_shared() = 0;
    };

    // weak内存块，仅有weak计数
    struct __ControlBlockWeak : public __ControlBlock {
        std::atomic<long> shared_weak_owners_;   // weak指针计数
        
        // 新增共享计数
        void add_shared() {
            __ControlBlock::add_shared();
        }

        // 新增weak计数
        void add_weak() {
            shared_weak_owners_.fetch_add(1, std::memory_order_relaxed);
        }

        // 减少共享计数
        void release_shared() {
            if(__ControlBlock::release_shared()) {
                release_weak();   
            }
        }

        // 减少weak计数
        void release_weak() {
            if(shared_weak_owners_.fetch_sub(1, std::memory_order_relaxed) == 0) {
                on_zero_weak();
            }
        }

        // 引用计数
        long use_count()
        {
            return __ControlBlock::use_count();
        }

        // weak用，加锁
        __ControlBlockWeak* lock()
        {
            // 自旋锁
            long current_shared_cnt = shared_owners_.load();
            while(current_shared_cnt != -1) {
                if(shared_owners_.compare_exchange_weak(current_shared_cnt, current_shared_cnt + 1))
                {
                    return this;
                }
            }

            return nullptr;
        }

        virtual void* get_deleter(const std::type_info&) const {
            return nullptr;
        }

        // weak计数清零
        virtual void on_zero_weak() = 0;
    };

    // 对象new构造方式所有控制块
    template <typename T, typename Deleter>
    struct __ControlBlockShared : public __ControlBlockWeak {
        Deleter deleter_;
        T* ptr_;

        virtual void* get_deleter(const std::type_info& info) const override;

        virtual void on_zero_shared() override
        {
            deleter_(ptr_);
            deleter_.~Deleter();
        }

        virtual void on_zero_weak() override
        {
            return;
        }
    };

    template <typename T, typename Deleter>
    void * __ControlBlockShared<T, Deleter>::get_deleter(const std::type_info& info) const {
        return typeid(Deleter) == info ? std::addressof(deleter_) : nullptr;
    }

    // make原地构造所用的控制块
    template <typename T>
    struct __ControlBlockInPlace : public __ControlBlockShared<T, DefaultDeleter<T>>{

        using _compressed_element = CompressedPairElement<T>;
        struct alignas(_compressed_element) _Storage {
            char storage_[sizeof(_compressed_element)];
        };

        _Storage storage_;
    };

    template <typename T>
    struct SharedPtr {
        using element_type = std::remove_extent_t<T>;
    private:
        element_type* ptr_element_;
        __ControlBlockWeak* ptr_control_block_;

    public:
        SharedPtr(std::nullptr_t) noexcept : T(nullptr), ptr_control_block_(nullptr) {};

        template <typename Y>
        requires std::is_convertible_v<Y, T>
        SharedPtr(Y* ptr) : ptr_element_(ptr)
        {
            using _ControlBlock = __ControlBlockShared<Y, DefaultDeleter<Y>>;
            // 先创建一个智能指针，防止new的时候抛出异常，内存没有被释放
            try {
            UniquePtr<Y> hold(ptr);
            ptr_control_block_ = new _ControlBlock(ptr_element_, DefaultDeleter<Y>()); 
            hold.release();
            }
            catch(...) {}
        }

        template <typename Y, typename YDeleter>
        requires std::is_convertible_v<Y, T>
        SharedPtr(Y* ptr, const YDeleter& deleter) : ptr_element_(ptr)
        {
            using _ControlBlock = __ControlBlockShared<Y, YDeleter>;
            try {
                ptr_control_block_ = new _ControlBlock(ptr_element_, deleter); 
            }
            catch(...) {}
        }

        ~SharedPtr()
        {
            if(ptr_control_block_)
            {
                ptr_control_block_->release_shared();
            }
        }
        // make_shared使用已有控制块构造
        template <typename Y, typename CntrlBlk>
        static std::shared_ptr<T> CreateWithControlBlock(Y* ptr_in, CntrlBlk* control_block) {
            
        };
    public:
        void reset()
        {
            SharedPtr().swap(*this);
        }

        template <typename Y>
        requires std::is_convertible_v<Y, T>
        void reset(Y* ptr)
        {
            SharedPtr(ptr).swap(*this);
        }

        template <typename Y, typename Deleter>
        void reset(Y* ptr, Deleter d)
        {
            SharedPtr(ptr, d).swap(*this);
        }

        // 交换
        void swap(SharedPtr& other)
        {
            std::swap(ptr_element_, other.ptr_element_);
            std::swap(ptr_control_block_, other.ptr_control_block_);
        }

        // 获取裸指针
        element_type* get() const
        {
            return ptr_element_;
        }

        // 解引用
        element_type& operator*() {
            return *ptr_element_;
        }

        // 指针操作符
        element_type* operator->()
        {
            return ptr_element_;
        }
        
    };

    template <typename T>
    struct WeakPtr {

    };

    template <typename T>
    struct EnableSharedFromThis {

    };

    template <typename T>
    SharedPtr<T> make_shared()
    {};
}