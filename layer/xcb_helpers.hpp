#pragma once

#include <X11/Xlib-xcb.h>
#include <xcb/composite.h>
#include <cstdio>
#include <optional>
#include <cstdlib>

namespace xcb {
  static thread_local constinit bool g_cache_bIsValid = false; //thread_local just incase g_cache could otherwise be accessed by one thread, while it is being deleted by another thread
  inline struct cookie_cache_t {
    xcb_window_t window;
    std::tuple<xcb_get_geometry_cookie_t, xcb_query_tree_cookie_t> cached_cookies;
    std::tuple<xcb_get_geometry_reply_t*, xcb_query_tree_reply_t*> cached_replies;
  } g_cache = {};
  
  template <typename T>
  concept CacheableCookie = std::is_same<T, xcb_get_geometry_cookie_t>::value || std::is_same<T, xcb_query_tree_cookie_t>::value;
  
  static constexpr auto replyFuncs = std::make_tuple(xcb_get_geometry_reply, xcb_query_tree_reply);
  template <CacheableCookie CookieType>
  consteval int getCacheTupleIdx() { return std::is_same<CookieType, xcb_get_geometry_cookie_t>::value ? 0 : 1; }
  
  //Note: this class is currently only meant to be used within GamescopeWSILayer::VkDeviceOverrides::QueuePresentKHR:
  struct Prefetcher {
    explicit Prefetcher(xcb_connection_t* __restrict__ connection, const xcb_window_t window) {
        g_cache = {
            .window = window,
            .cached_cookies = { 
                xcb_get_geometry(connection, window),
                xcb_query_tree(connection, window)
            }
        };
        g_cache_bIsValid = true;
    }

    ~Prefetcher() {
        g_cache_bIsValid = false;
        free(std::get<0>(g_cache.cached_replies));
        free(std::get<1>(g_cache.cached_replies));
        g_cache.cached_replies = {nullptr,nullptr};
    }
  };
  
  struct ReplyDeleter {
    const bool m_bOwning = true;
    consteval ReplyDeleter(bool bOwning = true) : m_bOwning{bOwning} {}
    template <typename T>
    void operator()(T* ptr) const {
      if (m_bOwning)
        free(const_cast<std::remove_const_t<T>*>(ptr));
    }
  };

  template <typename T>
  using Reply = std::unique_ptr<T, ReplyDeleter>;
  
  template <CacheableCookie CookieType, typename ReplyType>
  static Reply<ReplyType> getCachedReply(xcb_connection_t* __restrict__ connection, const CookieType cookie) {
    static constexpr int index = getCacheTupleIdx<CookieType>();
    if (std::get<index>(g_cache.cached_replies) == nullptr) {
        std::get<index>(g_cache.cached_replies) = std::get<index>(replyFuncs)(connection, cookie, nullptr);
    }

    return Reply<ReplyType>{std::get<index>(g_cache.cached_replies), ReplyDeleter{false}};
  }
  
  template <typename Cookie_RetType, typename Reply_RetType, typename XcbConn=xcb_connection_t*, typename... Args>
  class XcbFetch {
    using cookie_f_ptr_t = Cookie_RetType (*)(XcbConn, Args...);
    using reply_f_ptr_t = Reply_RetType (*)(XcbConn, Cookie_RetType, xcb_generic_error_t**);
    
    const cookie_f_ptr_t m_cookieFunc;
    const reply_f_ptr_t m_replyFunc;
    
    public:
        consteval XcbFetch(cookie_f_ptr_t cookieFunc, reply_f_ptr_t replyFunc) : m_cookieFunc{cookieFunc}, m_replyFunc{replyFunc} {}
        
        inline Reply<std::remove_pointer_t<Reply_RetType>> operator()(XcbConn conn, auto... args) { //have to use auto for argsTwo, since otherwise there'd be a type deduction conflict
            return Reply<std::remove_pointer_t<Reply_RetType>> { m_replyFunc(conn, m_cookieFunc(conn, args...), nullptr) };
        }
  };
  
  template <CacheableCookie Cookie_RetType, typename Reply_RetType>
  class XcbFetch<Cookie_RetType, Reply_RetType, xcb_connection_t*, xcb_window_t> {
    using Reply_RetTypeBase = std::remove_pointer_t<Reply_RetType>;
    using cookie_f_ptr_t = Cookie_RetType (*)(xcb_connection_t*, xcb_window_t);
    using reply_f_ptr_t = Reply_RetType (*)(xcb_connection_t*, Cookie_RetType, xcb_generic_error_t**);
    
    const cookie_f_ptr_t m_cookieFunc;
    const reply_f_ptr_t m_replyFunc;
    
    public:
        consteval XcbFetch(cookie_f_ptr_t cookieFunc, reply_f_ptr_t replyFunc) : m_cookieFunc{cookieFunc}, m_replyFunc{replyFunc} {}
        
        inline Reply<Reply_RetTypeBase> operator()(xcb_connection_t* conn, xcb_window_t window) {
            const bool tryCached = (g_cache_bIsValid && g_cache.window == window);
            if (tryCached) [[likely]]
                return getCachedReply<Cookie_RetType, Reply_RetTypeBase>(conn, std::get<Cookie_RetType>(g_cache.cached_cookies));

            return Reply<Reply_RetTypeBase> { m_replyFunc(conn, m_cookieFunc(conn, window), nullptr) };
        }
  };
 
  static std::optional<xcb_atom_t> getAtom(xcb_connection_t* connection, std::string_view name) {
    auto reply = XcbFetch{xcb_intern_atom, xcb_intern_atom_reply}(connection, false, name.length(), name.data());
    if (!reply) {
      fprintf(stderr, "[Gamescope WSI] Failed to get xcb atom.\n");
      return std::nullopt;
    }
    xcb_atom_t atom = reply->atom;
    return atom;
  }

  template <typename T>
  static std::optional<T> getPropertyValue(xcb_connection_t* connection, xcb_atom_t atom) {
    static_assert(sizeof(T) % 4 == 0);

    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;

    auto reply = XcbFetch{xcb_get_property, xcb_get_property_reply}(connection, false, screen->root, atom, XCB_ATOM_CARDINAL, 0, sizeof(T) / sizeof(uint32_t));
    if (!reply) {
      fprintf(stderr, "[Gamescope WSI] Failed to read T root window property.\n");
      return std::nullopt;
    }

    if (reply->type != XCB_ATOM_CARDINAL) {
      fprintf(stderr, "[Gamescope WSI] Atom of T was wrong type. Expected XCB_ATOM_CARDINAL.\n");
      return std::nullopt;
    }

    T value = *reinterpret_cast<const T *>(xcb_get_property_value(reply.get()));
    return value;
  }

  template <typename T>
  static std::optional<T> getPropertyValue(xcb_connection_t* connection, std::string_view name) {
    std::optional<xcb_atom_t> atom = getAtom(connection, name);
    if (!atom)
      return std::nullopt;

    return getPropertyValue<T>(connection, *atom);
  }

  static std::optional<xcb_window_t> getToplevelWindow(xcb_connection_t* connection, xcb_window_t window) {
    for (;;) {
      auto reply = XcbFetch{xcb_query_tree, xcb_query_tree_reply}(connection, window);

      if (!reply) {
        fprintf(stderr, "[Gamescope WSI] getToplevelWindow: xcb_query_tree failed for window 0x%x.\n", window);
        return std::nullopt;
      }

      if (reply->root == reply->parent)
        return window;

      window = reply->parent;
    }
  }

  static std::optional<VkRect2D> getWindowRect(xcb_connection_t* connection, xcb_window_t window) {
    auto reply = XcbFetch{xcb_get_geometry, xcb_get_geometry_reply}(connection, window);
    if (!reply) {
      fprintf(stderr, "[Gamescope WSI] getWindowRect: xcb_get_geometry failed for window 0x%x.\n", window);
      return std::nullopt;
    }

    VkRect2D rect = {
      .offset = { reply->x, reply->y },
      .extent = { reply->width, reply->height },
    };

    return rect;
  }

  static VkRect2D clip(VkRect2D parent, VkRect2D child) {
    return VkRect2D {
      .offset = child.offset,
      .extent = VkExtent2D {
        .width  = std::min<uint32_t>(child.extent.width,  std::max<int32_t>(parent.extent.width  - child.offset.x, 0)),
        .height = std::min<uint32_t>(child.extent.height, std::max<int32_t>(parent.extent.height - child.offset.y, 0)),
      },
    };
  }

  static VkExtent2D max(VkExtent2D a, VkExtent2D b) {
    return VkExtent2D {
      .width  = std::max<uint32_t>(a.width,  b.width),
      .height = std::max<uint32_t>(a.height, b.height),
    };
  }

  static std::optional<VkExtent2D> getLargestObscuringChildWindowSize(xcb_connection_t* connection, xcb_window_t window) {
    VkExtent2D largestExtent = {};

    auto reply = XcbFetch{xcb_query_tree, xcb_query_tree_reply}(connection, window);

    if (!reply) {
      fprintf(stderr, "[Gamescope WSI] getLargestObscuringWindowSize: xcb_query_tree failed for window 0x%x.\n", window);
      return std::nullopt;
    }

    auto ourRect = getWindowRect(connection, window);
    if (!ourRect) {
      fprintf(stderr, "[Gamescope WSI] getLargestObscuringWindowSize: getWindowRect failed for main window 0x%x.\n", window);
      return std::nullopt;
    }

    xcb_window_t* children = xcb_query_tree_children(reply.get());
    for (uint32_t i = 0; i < reply->children_len; i++) {
      xcb_window_t child = children[i];

      auto attributeReply = XcbFetch{xcb_get_window_attributes, xcb_get_window_attributes_reply}(connection, child);

      const bool obscuring =
        attributeReply &&
        attributeReply->map_state == XCB_MAP_STATE_VIEWABLE &&
        !attributeReply->override_redirect;

      if (obscuring) {
        if (auto childRect = getWindowRect(connection, child)) {
          VkRect2D clippedRect = clip(*ourRect, *childRect);
          largestExtent = max(largestExtent, clippedRect.extent);
        }
      }
    }

    return largestExtent;
  }

}

inline int32_t iabs(int32_t a) {
  if (a < 0)
    return -a;

  return a;
}
