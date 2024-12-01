#include <cstddef>
#include <memory>
#include <type_traits>
#include <iostream>
#include <typeinfo>
#include <atomic>
#include <alloca.h>

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
        long use_count() {
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

        virtual const void* get_deleter(const std::type_info&) const {
            return nullptr;
        }

        // weak计数清零
        virtual void on_zero_weak() = 0;
    };

    // 对象new构造方式所有控制块
    template <typename T, typename Deleter>
    struct __ControlBlockShared : public __ControlBlockWeak {
        Deleter deleter_;
        T* element_ptr_;

        virtual const void* get_deleter(const std::type_info& info) const override;

        virtual void on_zero_shared() override
        {
            deleter_(element_ptr_);
            deleter_.~Deleter();
        }

        virtual void on_zero_weak() override
        {
            return;
        }
    };

    template <typename T, typename Deleter>
    const void * __ControlBlockShared<T, Deleter>::get_deleter(const std::type_info& info) const {
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
    class WeakPtr;

    template <typename T>
    class SharedPtr;

    template <typename T>
    struct EnableSharedFromThis {
        friend class SharedPtr<T>;
    private:
        WeakPtr<T> weak_this_;
    protected:
        EnableSharedFromThis() {}
        EnableSharedFromThis(const EnableSharedFromThis&) {};
        EnableSharedFromThis& operator=(const EnableSharedFromThis&) { return *this; };
    public:
        ~EnableSharedFromThis() {}
        SharedPtr<T> shared_from_this() {
            return SharedPtr<T>(weak_this_);
        }
    };

    template <typename T>
    struct SharedPtr {
        template <typename U>
        friend class WeakPtr;
        using element_type = std::remove_extent_t<T>;
    private:
        element_type* ptr_element_;
        __ControlBlockWeak* ptr_control_block_;

    public:
        SharedPtr() noexcept: ptr_element_(nullptr), ptr_control_block_(nullptr) {};
        SharedPtr(std::nullptr_t) noexcept : ptr_element_(nullptr), ptr_control_block_(nullptr) {};

        template <typename Y>
        requires std::is_convertible_v<Y, T>
        SharedPtr(Y* ptr) : ptr_element_(ptr)
        {
            using _ControlBlock = __ControlBlockShared<Y, DefaultDeleter<Y>>;
            // 先创建一个智能指针，防止new的时候抛出异常，内存没有被释放
            try {
            UniquePtr<Y> hold(ptr);
            ptr_control_block_ = new _ControlBlock(ptr_element_, DefaultDeleter<Y>()); 
            __enable_weak_this(ptr_element_, ptr_element_);
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
                __enable_weak_this(ptr_element_, ptr_element_);
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
        static SharedPtr<T> CreateWithControlBlock(Y* ptr_in, CntrlBlk* ptr_control_block) {
            SharedPtr<T> r;
            r.ptr_element_  = ptr_in;
            r.ptr_control_block_ = ptr_control_block;

            return r;
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

        template <typename Yp, typename OrigPtr>
        requires std::is_convertible_v<OrigPtr, EnableSharedFromThis<Yp>>
        void __enable_weak_this(const EnableSharedFromThis<Yp>* e, OrigPtr* ptr) noexcept
        {
            if(e && e->weak_this->expired())
            {
                e->weak_this_ = SharedPtr<Yp>(*this, ptr);
            }
        };

        void __enable_weak_this(...) noexcept {};
    };

    template <typename T>
    struct WeakPtr {
        using element_type = std::remove_extent_t<T>;
        template <typename U>
        friend class SharedPtr;
    private:
        element_type* ptr_element_;
        __ControlBlockWeak* ptr_control_block_;
    public:
        WeakPtr() noexcept
            : ptr_element_(nullptr)
            , ptr_control_block_(nullptr)
        {}

        WeakPtr(const WeakPtr& r) 
            : ptr_element_(r.ptr_element_)
            , ptr_control_block_(r.ptr_control_block_) {
            if(ptr_control_block_) {
                ptr_control_block_->add_weak();
            }
        }

        template <typename Y>
        requires std::is_convertible_v<Y, T>
        WeakPtr(const WeakPtr<Y>& r) noexcept 
            : ptr_element_(nullptr)
            , ptr_control_block_(nullptr) {
            SharedPtr<Y> s = r.lock();
            *this = WeakPtr<T>(s);
        }

        template <typename Y>
        requires std::is_convertible_v<Y, T>
        WeakPtr(const SharedPtr<Y>& r) noexcept
            : ptr_element_(r.ptr_element_)
            , ptr_control_block_(r.ptr_control_block_) {
            if(ptr_control_block_) {
                ptr_control_block_->add_weak();
            } 
        }

        template <typename Y>
        requires std::is_convertible_v<Y, T>
        WeakPtr(WeakPtr<Y>&& r) noexcept 
            : ptr_element_(nullptr)
            , ptr_control_block_(nullptr) {
            SharedPtr<Y> s = r.lock();
            *this = WeakPtr<T>(s);
            s.reset();
        }

        ~WeakPtr() {
            if(ptr_control_block_) {
                ptr_control_block_->release_weak();
            }
        }

        WeakPtr& operator=(const WeakPtr& r) noexcept {
            WeakPtr(r).swap(*this);

            return *this;
        }

        WeakPtr& operator=(WeakPtr&& r) noexcept {
            WeakPtr(std::move(r)).swap(*this);

            return *this;
        }

        template <typename Y>
        requires std::is_convertible_v<Y, T>
        WeakPtr& operator=(const WeakPtr<Y>& r) noexcept {
            WeakPtr(r).swap(*this);

            return *this;
        }

        template <typename Y>
        requires std::is_convertible_v<Y, T>
        WeakPtr& operator=(const SharedPtr<Y>& r) noexcept {
            WeakPtr(r).swap(*this);

            return *this;
        }

        template <typename Y>
        requires std::is_convertible_v<Y, T>
        WeakPtr& operator=(WeakPtr<Y>&& r) noexcept {
            WeakPtr(std::move(r)).swap(*this);

            return *this;
        }

        // 释放所有权
        void reset() noexcept
        {
            WeakPtr().swap(*this);
        }

        // 交换
        void swap(WeakPtr& r) {
            std::swap(ptr_element_, r.ptr_element_);
            std::swap(ptr_control_block_, r.ptr_control_block_);
        }

        // 返回共享所有权的SharedPtr的数量
        long use_count() const noexcept {
            return ptr_control_block_ == nullptr ? 
                0 : ptr_control_block_->use_count();
        }

        // 等价于use_count == 0
        bool expired() const noexcept {
            return use_count() == 0 || ptr_control_block_ == nullptr;
        }

        // 创建对应的SharedPtr
        // 若当前无管理对象，返回空(expired=true)
        SharedPtr<T> lock() const noexcept {
            SharedPtr<T> r;
            r.ptr_control_block_ = ptr_control_block_ ? ptr_control_block_->lock() : ptr_control_block_;
            if(r.ptr_control_block_) {
                r.ptr_element_ = ptr_element_;
            }

            return r;
        }


    };



    template <typename T>
    SharedPtr<T> make_shared() {
        using __ControlBlock = __ControlBlockInPlace<T>;
        using __ControlBlockAllocator = std::allocator<__ControlBlock>;
        __ControlBlockAllocator alloc;
        auto ptr_alloc_memory = alloc.allocate(1);
        auto ptr_control_block = new ((void*)ptr_alloc_memory) __ControlBlock;
        
        return SharedPtr<T>::CreateWithControlBlock(ptr_control_block->element_ptr_, std::addressof(*ptr_control_block));
    };
}