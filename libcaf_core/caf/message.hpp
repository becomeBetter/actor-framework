/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2015                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#ifndef CAF_MESSAGE_HPP
#define CAF_MESSAGE_HPP

#include <tuple>
#include <sstream>
#include <type_traits>

#include "caf/fwd.hpp"
#include "caf/atom.hpp"
#include "caf/config.hpp"
#include "caf/optional.hpp"
#include "caf/make_counted.hpp"
#include "caf/skip_message.hpp"
#include "caf/index_mapping.hpp"
#include "caf/allowed_unsafe_message_type.hpp"

#include "caf/detail/int_list.hpp"
#include "caf/detail/apply_args.hpp"
#include "caf/detail/comparable.hpp"
#include "caf/detail/tuple_vals.hpp"
#include "caf/detail/type_traits.hpp"
#include "caf/detail/message_data.hpp"
#include "caf/detail/implicit_conversions.hpp"

namespace caf {
class message_handler;

/// Describes a fixed-length, copy-on-write, type-erased
/// tuple with elements of any type.
class message {
public:
  /// Creates an empty message.
  message() = default;

  /// Move constructor.
  message(message&&);

  /// Copy constructor.
  message(const message&) = default;

  /// Move assignment.
  message& operator=(message&&);

  /// Copy assignment.
  message& operator=(const message&) = default;

  /// Returns the size of this message.
  inline size_t size() const {
    return vals_ ? vals_->size() : 0;
  }

  /// Creates a new message with all but the first n values.
  message drop(size_t n) const;

  /// Creates a new message with all but the last n values.
  message drop_right(size_t n) const;

  /// Creates a new message from the first n values.
  inline message take(size_t n) const {
    return n >= size() ? *this : drop_right(size() - n);
  }

  /// Creates a new message from the last n values.
  inline message take_right(size_t n) const {
    return n >= size() ? *this : drop(size() - n);
  }

  /// Creates a new message of size `n` starting at the element at position `p`.
  message slice(size_t p, size_t n) const;

  /// Concatinate messages
  template <class... Ts>
  static message concat(const Ts&... xs) {
    return concat_impl({xs.vals()...});
  }

  /// Returns a mutable pointer to the element at position `p`.
  void* mutable_at(size_t p);

  /// Returns a const pointer to the element at position `p`.
  const void* at(size_t p) const;

  /// Returns @c true if `*this == other, otherwise false.
  bool equals(const message& other) const;

  /// Returns true if `size() == 0, otherwise false.
  inline bool empty() const {
    return size() == 0;
  }

  /// Returns the value at position `p` as const reference of type `T`.
  template <class T>
  const T& get_as(size_t p) const {
    CAF_ASSERT(match_element(p, detail::type_nr<T>::value, &typeid(T)));
    return *reinterpret_cast<const T*>(at(p));
  }

  /// Returns the value at position `p` as mutable reference of type `T`.
  template <class T>
  T& get_as_mutable(size_t p) {
    CAF_ASSERT(match_element(p, detail::type_nr<T>::value, &typeid(T)));
    return *reinterpret_cast<T*>(mutable_at(p));
  }

  /// Returns `handler(*this)`.
  optional<message> apply(message_handler handler);

  /// Filters this message by applying slices of it to `handler` and  returns
  /// the remaining elements of this operation. Slices are generated in the
  /// sequence `[0, size)`, `[0, size-1)`, `...` , `[1, size-1)`, `...`,
  /// `[size-1, size)`. Whenever a slice matches, it is removed from the message
  /// and the next slice starts at the *same* index on the reduced message.
  ///
  /// For example:
  ///
  /// ~~~
  /// auto msg = make_message(1, 2.f, 3.f, 4);
  /// // extract float and integer pairs
  /// auto msg2 = msg.extract({
  ///   [](float, float) { },
  ///   [](int, int) { }
  /// });
  /// assert(msg2 == make_message(1, 4));
  /// ~~~
  ///
  /// Step-by-step explanation:
  /// - Slice 1: `(1, 2.f, 3.f, 4)`, no match
  /// - Slice 2: `(1, 2.f, 3.f)`, no match
  /// - Slice 3: `(1, 2.f)`, no match
  /// - Slice 4: `(1)`, no match
  /// - Slice 5: `(2.f, 3.f, 4)`, no match
  /// - Slice 6: `(2.f, 3.f)`, *match*; new message is `(1, 4)`
  /// - Slice 7: `(4)`, no match
  ///
  /// Slice 7 is `(4)`, i.e., does not contain the first element, because the
  /// match on slice 6 occurred at index position 1. The function `extract`
  /// iterates a message only once, from left to right.
  message extract(message_handler handler) const;

  /// Stores the name of a command line option ("<long name>[,<short name>]")
  /// along with a description and a callback.
  struct cli_arg {
    /// Returns `true` on a match, `false` otherwise.
    using consumer = std::function<bool (const std::string&)>;

    /// Full name of this CLI argument using format "<long name>[,<short name>]"
    std::string name;

    /// Desciption of this CLI argument for the auto-generated help text.
    std::string text;

    /// Auto-generated helptext for this item.
    std::string helptext;

    /// Evaluates option arguments.
    consumer fun;

    /// Creates a CLI argument without data.
    cli_arg(std::string name, std::string text);

    /// Creates a CLI argument storing its matched argument in `dest`.
    cli_arg(std::string name, std::string text, atom_value& dest);

    /// Creates a CLI argument storing its matched argument in `dest`.
    cli_arg(std::string name, std::string text, std::string& dest);

    /// Creates a CLI argument appending matched arguments to `dest`.
    cli_arg(std::string name, std::string text, std::vector<std::string>& dest);

    /// Creates a CLI argument using the function object `f`.
    cli_arg(std::string name, std::string text, consumer f);

    /// Creates a CLI argument for converting from strings,
    /// storing its matched argument in `dest`.
    template <class T>
    cli_arg(typename std::enable_if<
              detail::type_nr<T>::value != 0,
              std::string
            >::type name,
            std::string text, T& dest);

    /// Creates a CLI argument for converting from strings,
    /// appending matched arguments to `dest`.
    template <class T>
    cli_arg(std::string name, std::string text, std::vector<T>& dest);
  };

  struct cli_res;

  using help_factory = std::function<std::string (const std::vector<cli_arg>&)>;

  /// A simplistic interface for using `extract` to parse command line options.
  /// Usage example:
  ///
  /// ~~~
  /// int main(int argc, char** argv) {
  ///   uint16_t port;
  ///   string host = "localhost";
  ///   auto res = message_builder(argv + 1, argv + argc).extract_opts({
  ///     {"port,p", "set port", port},
  ///     {"host,H", "set host (default: localhost)", host},
  ///     {"verbose,v", "enable verbose mode"}
  ///   });
  ///   if (!res.error.empty()) {
  ///     cerr << res.error << endl;
  ///     return 1;
  ///   }
  ///   if (res.opts.count("help") > 0) {
  ///     // CLI arguments contained "-h", "--help", or "-?" (builtin);
  ///     cout << res.helptext << endl;
  ///     return 0;
  ///   }
  ///   if (!res.remainder.empty()) {
  ///     // ... extract did not consume all CLI arguments ...
  ///   }
  ///   if (res.opts.count("verbose") > 0) {
  ///     // ... enable verbose mode ...
  ///   }
  ///   // ...
  /// }
  /// ~~~
  /// @param xs List of argument descriptors.
  /// @param help_generator Optional factory function to generate help text
  ///                       (overrides the default generator).
  /// @param suppress_help Suppress generation of default-generated help option.
  /// @returns A struct containing remainder
  ///          (i.e. unmatched elements), a set containing the names of all
  ///          used arguments, and the generated help text.
  /// @throws std::invalid_argument if no name or more than one long name is set
  cli_res extract_opts(std::vector<cli_arg> xs,
                       help_factory help_generator = nullptr,
                       bool suppress_help = false) const;

  /// Queries whether the element at position `p` is of type `T`.
  template <class T>
  bool match_element(size_t p) const {
    const std::type_info* rtti = nullptr;
    if (detail::type_nr<T>::value == 0) {
      rtti = &typeid(T);
    }
    return match_element(p, detail::type_nr<T>::value, rtti);
  }

  /// Queries whether the types of this message are `Ts...`.
  template <class... Ts>
  bool match_elements() const {
    std::integral_constant<size_t, 0> p0;
    detail::type_list<Ts...> tlist;
    return size() == sizeof...(Ts) && match_elements_impl(p0, tlist);
  }

  message& operator+=(const message& x);

  /// @cond PRIVATE

  using raw_ptr = detail::message_data*;

  using data_ptr = detail::message_data::cow_ptr;

  explicit message(const data_ptr& vals);

  inline data_ptr& vals() {
    return vals_;
  }

  inline const data_ptr& vals() const {
    return vals_;
  }

  inline const data_ptr& cvals() const {
    return vals_;
  }

  inline uint32_t type_token() const {
    return vals_ ? vals_->type_token() : 0xFFFFFFFF;
  }

  inline void force_unshare() {
    vals_.unshare();
  }

  void reset(raw_ptr new_ptr = nullptr, bool add_ref = true);

  void swap(message& other);

  bool match_element(size_t p, uint16_t tnr, const std::type_info* rtti) const;

  template <class T, class... Ts>
  bool match_elements(detail::type_list<T, Ts...> list) const {
    std::integral_constant<size_t, 0> p0;
    return size() == (sizeof...(Ts) + 1) && match_elements_impl(p0, list);
  }

  /// @endcond

private:
  template <size_t P>
  static bool match_elements_impl(std::integral_constant<size_t, P>,
                                  detail::type_list<>) {
    return true; // end of recursion
  }

  template <size_t P, class T, class... Ts>
  bool match_elements_impl(std::integral_constant<size_t, P>,
                           detail::type_list<T, Ts...>) const {
    std::integral_constant<size_t, P + 1> next_p;
    detail::type_list<Ts...> next_list;
    return match_element<T>(P) && match_elements_impl(next_p, next_list);
  }

  message extract_impl(size_t start, message_handler handler) const;

  static message concat_impl(std::initializer_list<data_ptr> ptrs);

  data_ptr vals_;
};

/// @relates message
void serialize(serializer& sink, const message& msg, const unsigned int);

/// @relates message
void serialize(deserializer& sink, message& msg, const unsigned int);

/// @relates message
std::string to_string(const message& msg);

/// Stores the result of `message::extract_opts`.
struct message::cli_res {
  /// Stores the remaining (unmatched) arguments.
  message remainder;
  /// Stores the names of all active options.
  std::set<std::string> opts;
  /// Stores the automatically generated help text.
  std::string helptext;
  /// Stores errors during option parsing.
  std::string error;
};

/// @relates message
inline bool operator==(const message& lhs, const message& rhs) {
  return lhs.equals(rhs);
}

/// @relates message
inline bool operator!=(const message& lhs, const message& rhs) {
  return !(lhs == rhs);
}

/// @relates message
inline message operator+(const message& lhs, const message& rhs) {
  return message::concat(lhs, rhs);
}

/// Unboxes atom constants, i.e., converts `atom_constant<V>` to `V`.
/// @relates message
template <class T, int IsPlaceholderRes = std::is_placeholder<T>::value>
struct unbox_message_element {
  using type = index_mapping;
};

template <class T>
struct unbox_message_element<T, 0> {
  using type = T;
};

template <atom_value V>
struct unbox_message_element<atom_constant<V>, 0> {
  using type = atom_value;
};

///
template <class T>
struct is_serializable_or_whitelisted {
  static constexpr bool value = detail::is_serializable<T>::value
                                || allowed_unsafe_message_type<T>::value;
};

/// Returns a new `message` containing the values `(x, xs...)`.
/// @relates message
template <class T, class... Ts>
typename std::enable_if<
  ! std::is_same<message, typename std::decay<T>::type>::value
  || (sizeof...(Ts) > 0),
  message
>::type
make_message(T&& x, Ts&&... xs) {
  using namespace caf::detail;
  using stored_types =
    type_list<
      typename unbox_message_element<
        typename strip_and_convert<T>::type
      >::type,
      typename unbox_message_element<
        typename strip_and_convert<Ts>::type
      >::type...
    >;
  static_assert(tl_forall<stored_types, is_serializable_or_whitelisted>::value,
                "at least one type is not serializable via free "
                "'serialize(Processor&, T&, const unsigned int)' or "
                "`T::serialize(Processor&, const unsigned int)` "
                "member function; you can whitelist individual types by "
                "specializing `caf::allowed_unsafe_message_type<T>` "
                "or using the macro CAF_ALLOW_UNSAFE_MESSAGE_TYPE");
  using storage = typename tl_apply<stored_types, tuple_vals>::type;
  auto ptr = make_counted<storage>(std::forward<T>(x), std::forward<Ts>(xs)...);
  return message{detail::message_data::cow_ptr{std::move(ptr)}};
}

/// Returns a copy of @p other.
/// @relates message
inline message make_message(message other) {
  return other;
}

/// Returns an empty `message`.
/// @relates message
inline message make_message() {
  return message{};
}

/******************************************************************************
 *                  template member function implementations                  *
 ******************************************************************************/

template <class T>
message::cli_arg::cli_arg(typename std::enable_if<
                            detail::type_nr<T>::value != 0,
                            std::string
                          >::type
                          nstr, std::string tstr, T& arg)
    : name(std::move(nstr)),
      text(std::move(tstr)),
      fun([&arg](const std::string& str) -> bool {
        T x;
        // TODO: using this stream is a workaround for the missing
        //       from_string<T>() interface and has downsides such as
        //       not performing overflow/underflow checks etc.
        std::istringstream iss{str};
        if (iss >> x) {
          arg = x;
          return true;
        }
        return false;
      }) {
  // nop
}

template <class T>
message::cli_arg::cli_arg(std::string nstr, std::string tstr, std::vector<T>& arg)
    : name(std::move(nstr)),
      text(std::move(tstr)),
      fun([&arg](const std::string& str) -> bool {
        T x;
        std::istringstream iss{str};
        if (iss >> x) {
          arg.emplace_back(std::move(x));
          return true;
        }
        return false;
          }) {
  // nop
}

} // namespace caf

#endif // CAF_MESSAGE_HPP
