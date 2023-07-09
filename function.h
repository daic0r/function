#ifndef FUNCTION_H
#define FUNCTION_H

#include <cstddef>
#include <array>
#include <debug/assertions.h>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <memory>
#include <utility>

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
   class function<R(Args...)> {
      static constexpr std::size_t ALIGNMENT = 16;
      static constexpr std::size_t BUFFER_SIZE = 128;

      using buf_t = std::array<std::byte, BUFFER_SIZE>;
      class concept_interface;

      alignas(ALIGNMENT) std::variant<std::monostate, buf_t, std::unique_ptr<concept_interface>> m_data;

   public:
      function() = default;
      template<typename F, std::enable_if_t<not std::is_same_v<std::decay_t<F>, function>, bool> = true>
      function(F&& f) {
         static_assert(alignof(F) <= ALIGNMENT, "type is overaligned");
         static_assert(std::is_invocable_r_v<R, std::decay_t<F>, Args...>, "invocable does not match specified signature");
         
         if constexpr(sizeof(std::decay_t<F>) <= BUFFER_SIZE) {
#ifdef DEBUG
            dbgOut("Using SBO");
#endif
            auto& buf = m_data.template emplace<buf_t>();
            new (buf.data()) concept_impl<std::decay_t<F>>{ std::forward<F>(f) };
         } else {
#ifdef DEBUG
            dbgOut("Using dynamic allocation");
#endif
            m_data = std::make_unique<concept_impl<std::decay_t<F>>>(std::forward<F>(f));
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
         : m_data{ std::exchange(rhs.m_data, std::monostate{}) }
      {
#ifdef DEBUG
         dbgOut("Move ctor");
#endif
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
         std::swap(m_data, rhs.m_data);
      }

      friend void swap(function& lhs, function& rhs) noexcept {
         lhs.swap(rhs);
      }

      R operator()(Args... args) {
         return std::visit(overload{
               [](std::monostate) -> R {
                  throw std::runtime_error("no callable contained");
               },
               [this, ... args = std::forward<Args>(args)](auto&&) 
               { 
                  return ptr()->invoke(args...); 
               }, 
         }, m_data);
      }

   private:
      concept_interface* ptr() noexcept {
         return std::visit(overload{
               [](std::monostate) -> concept_interface* {
                  return nullptr;
               },
               [this](buf_t& buf) {
                  return reinterpret_cast<concept_interface*>(buf.data());
               },
               [this](std::unique_ptr<concept_interface>& ptr) {
                  return ptr.get();
               }
            }, m_data);
      }
      concept_interface const* ptr() const noexcept {
         return std::visit(overload{
               [](std::monostate) -> concept_interface const* {
                  return nullptr;
               },
               [this](buf_t const& buf) {
                  return reinterpret_cast<concept_interface const*>(buf.data());
               },
               [this](std::unique_ptr<concept_interface> const& ptr) -> concept_interface const* {
                  return ptr.get();
               }
            }, m_data);
      }

      class concept_interface {
      public:
         virtual ~concept_interface() = default;
         virtual std::unique_ptr<concept_interface> clone() const = 0;
         virtual void clone(buf_t& mem) const = 0;
         virtual R invoke(Args...) = 0;
      };

      template<typename Func>
      class concept_impl : public concept_interface {
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

         R invoke(Args... args) override {
            return m_func(args...);
         }
      private:
         Func m_func;
      };
   };

}

#endif
