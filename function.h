#ifndef FUNCTION_H
#define FUNCTION_H

#include <cstddef>
#include <array>
#include <type_traits>
#include <variant>
#include <memory>
#include <utility>
#include <tuple>
#include <stdexcept>

#ifdef DEBUG
#include <iostream>
#include <string_view>

void dbgOut(std::string_view msg) {
   std::cout << "*DEBUG: " << msg << std::endl;
}
#endif

template<typename... Ts>
struct overload : public Ts... {
   using Ts::operator()...;
};

template<typename... Ts>
overload(Ts...) -> overload<Ts...>;

namespace ice {
   template<typename T> class function;

   template<typename R, typename... Args>
   class function<R(Args...)> final {
      static constexpr std::size_t ALIGNMENT = 16;
      static constexpr std::size_t BUFFER_SIZE = 128;

      using buf_t = std::array<std::byte, BUFFER_SIZE>;
      class concept_interface;

      alignas(ALIGNMENT) std::variant<std::monostate, buf_t, std::unique_ptr<concept_interface>> m_data;

   public:
      function() = default;
      template<typename F, std::enable_if_t<not std::is_same_v<std::decay_t<F>, function>, bool> = true>
      function(F&& f) {
         using stripped_t = std::decay_t<F>;
         static_assert(alignof(stripped_t) <= ALIGNMENT, "type is overaligned");
         static_assert(std::is_invocable_r_v<R, stripped_t, Args...>, "invocable does not match specified signature");
         
         if constexpr(sizeof(stripped_t) <= BUFFER_SIZE) {
#ifdef DEBUG
            dbgOut("Using SBO");
#endif
            auto& buf = m_data.template emplace<buf_t>();
            new (buf.data()) concept_impl<stripped_t>{ std::forward<F>(f) };
         } else {
#ifdef DEBUG
            dbgOut("Using dynamic allocation");
#endif
            m_data = std::make_unique<concept_impl<stripped_t>>(std::forward<F>(f));
         }
      }
      function(function const& rhs) {
#ifdef DEBUG
         dbgOut("Copy ctor");
#endif
         std::visit(overload {
            [](std::monostate) {},
            [&rhs, this](buf_t const&) {
               auto& buf = m_data.template emplace<buf_t>();
               rhs.ptr()->clone(buf);
            },
            [&rhs, this](std::unique_ptr<concept_interface> const&) {
               m_data.template emplace<std::unique_ptr<concept_interface>>(rhs.ptr()->clone());
            }
         }, rhs.m_data);
      }
      function& operator=(function const& rhs) {
#ifdef DEBUG
         dbgOut("Copy-assigning");
#endif
         function copy{ rhs };
         copy.swap(*this);
         return *this;
      }
      function(function&& rhs) noexcept
      {
#ifdef DEBUG
         dbgOut("Move ctor");
#endif
         moveHelper(std::move(rhs), *this);
      }
      function& operator=(function&& rhs) noexcept {
#ifdef DEBUG
         dbgOut("Move-assigning");
#endif
         function copy{ std::move(rhs) };
         copy.swap(*this);
         return *this;
      }
      ~function() {
         std::visit(overload{
               [this](buf_t&) {
#ifdef DEBUG
                  dbgOut("Manual dtor invocation");
#endif
                  ptr()->~concept_interface(); 
               },
               [](auto&&) {
               }
            }, m_data);
      }

      void swap(function& rhs) noexcept {
         function tmp{ std::move(rhs) };
         moveHelper(std::move(*this), rhs);
         moveHelper(std::move(tmp), *this);
      }

      friend void swap(function& lhs, function& rhs) noexcept {
         lhs.swap(rhs);
      }

      template<typename... Ts>
      R operator()(Ts&&... args) {
#ifdef DEBUG
         std::cout << "*DEBUG: Calling with variant index " << m_data.index() << std::endl;
#endif
         return std::visit(overload{
               [](std::monostate) mutable -> R {
                  throw std::runtime_error("no callable contained");
               },
               // C++20 version below
               //[this, ... args = std::forward<Args>(args)](auto&&) 
               [this, args = std::make_tuple(std::forward<Ts>(args)...)](auto&&) mutable -> R {
                  return std::apply([this](auto&&... args)
                     { 
                        return ptr()->invoke(std::forward<decltype(args)>(args)...); 
                     },
                     std::move(args)
                  );
               }
         }, m_data);
      }

   private:
      template<typename Self>
      static auto ptr(Self&& self) noexcept {
         constexpr auto bIsConst = std::is_const_v<std::remove_reference_t<Self>>;
         using ret_t = std::conditional_t<bIsConst, concept_interface const*, concept_interface*>;
         return std::visit(overload{
               [](std::monostate) -> ret_t {
                  return nullptr;
               },
               [](std::conditional_t<bIsConst, buf_t const&, buf_t&> buf) {
                  return reinterpret_cast<ret_t>(buf.data());
               },
               [](std::conditional_t<bIsConst, std::unique_ptr<concept_interface> const&, std::unique_ptr<concept_interface>&> ptr) -> ret_t {
                  return ptr.get();
               }
            }, self.m_data);
      }
      concept_interface* ptr() noexcept {
#ifdef DEBUG
         dbgOut("Getting ptr to mutable");
#endif
         return ptr(*this);
      }
      concept_interface const* ptr() const noexcept {
#ifdef DEBUG
         dbgOut("Getting ptr to const");
#endif
         return ptr(*this);
      }

      bool isTriviallyMoveConstructible() const noexcept {
         return ptr() != nullptr ? ptr()->isTriviallyMoveConstructible() : true;
      }

      static void moveHelper(function&& from, function& to) {
         std::visit(overload {
            [](std::monostate) {
#ifdef DEBUG
               dbgOut("\tMoving from monostate");
#endif
            },
            [&from, &to](buf_t&&) {
#ifdef DEBUG
               dbgOut("\tmoveCloning from buffer");
#endif
               auto& buf = to.m_data.template emplace<buf_t>();
               if (from.isTriviallyMoveConstructible()) {
                  to.m_data = std::exchange(from.m_data, std::monostate{});
               }
               else {
                  from.ptr()->cloneMove(buf);
                  from.m_data.template emplace<std::monostate>();
               }
            },
            [&from, &to](std::unique_ptr<concept_interface>&&) {
#ifdef DEBUG
               dbgOut("\tMoving unique_ptr");
#endif
               to.m_data = std::exchange(from.m_data, std::monostate{});
            }
         }, std::move(from.m_data));
      }

      class concept_interface {
      public:
         virtual ~concept_interface() = default;
         virtual std::unique_ptr<concept_interface> clone() const = 0;
         virtual void clone(buf_t& mem) const = 0;
         virtual void cloneMove(buf_t& mem) = 0;
         virtual bool isTriviallyMoveConstructible() const noexcept = 0;
         virtual R invoke(Args...) = 0;
      };

      template<typename Func>
      class concept_impl final : public concept_interface {
      public:
         template<typename F>
         concept_impl(F&& f) : m_func{ std::forward<F>(f) } {}
         concept_impl(concept_impl const&) = default;
         concept_impl& operator=(concept_impl const&) = default;
         concept_impl(concept_impl&&) = default;
         concept_impl& operator=(concept_impl&&) = default;
         ~concept_impl() = default;

         std::unique_ptr<concept_interface> clone() const override {
            return std::make_unique<concept_impl>(*this);
         }

         void clone(buf_t& mem) const override {
            new (mem.data()) concept_impl{ *this };
         }

         void cloneMove(buf_t& mem) override {
            new (mem.data()) concept_impl{ std::move(*this) };
         }

         constexpr bool isTriviallyMoveConstructible() const noexcept {
            return std::is_trivially_move_constructible_v<Func>;
         }

         R invoke(Args... args) override {
            return m_func(args...);
         }
      private:
         Func m_func;
      };
   };

}

#endif
