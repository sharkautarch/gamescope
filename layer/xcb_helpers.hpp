#pragma once

#include <X11/Xlib-xcb.h>
#include <xcb/composite.h>
#include <cstdio>
#include <optional>

namespace xcb {

  typedef struct {
    xcb_query_tree_cookie_t q_tree;
    xcb_get_geometry_cookie_t geo;
    xcb_window_t window;
    std::tuple<xcb_query_tree_reply_t*, xcb_get_geometry_reply_t*> cached_replies;
  } cookie_cache_t;

  static thread_local constinit bool g_cache_bIsValid = false; //thread_local just incase g_cache could otherwise be accessed by one thread, while it is being deleted by another thread
  static constinit cookie_cache_t g_cache = {};
  
  struct ReplyDeleter {
    template <typename T>
    void operator()(T* ptr) const {
      free(const_cast<std::remove_const_t<T>*>(ptr));
    }
  };

  template <typename T>
  using Reply = std::unique_ptr<T, ReplyDeleter>;
  
  
  Reply<xcb_query_tree_reply_t> CachedReply(xcb_connection_t* connection, xcb_query_tree_cookie_t cookie)
  {
    xcb_query_tree_reply_t* reply_copied = reinterpret_cast<xcb_query_tree_reply_t*>(malloc(sizeof(xcb_query_tree_reply_t)));
    if (std::get<0>(g_cache.cached_replies) == nullptr) {
        std::get<0>(g_cache.cached_replies) = xcb_query_tree_reply(connection, cookie, nullptr);
    }

    memcpy(reinterpret_cast<void*>(reply_copied), reinterpret_cast<void*>(std::get<0>(g_cache.cached_replies)), sizeof(xcb_query_tree_reply_t));

    return Reply<xcb_query_tree_reply_t>{reply_copied};;
  }

  Reply<xcb_get_geometry_reply_t> CachedReply(xcb_connection_t* connection, xcb_get_geometry_cookie_t cookie)
  {
    xcb_get_geometry_reply_t* reply_copied = reinterpret_cast<xcb_get_geometry_reply_t*>(malloc(sizeof(xcb_get_geometry_reply_t)));
    if (std::get<1>(g_cache.cached_replies) == nullptr) {
        std::get<1>(g_cache.cached_replies) = xcb_get_geometry_reply(connection, cookie, nullptr);
    }

    memcpy(reinterpret_cast<void*>(reply_copied), reinterpret_cast<void*>(std::get<1>(g_cache.cached_replies)), sizeof(xcb_get_geometry_reply_t));

    return Reply<xcb_get_geometry_reply_t>{reply_copied};
  }

  class Prefetcher
  {
  	//Note: this class is currently only meant to be used within GamescopeWSILayer::VkDeviceOverrides::QueuePresentKHR
  	
  	//The g_class struct has a thread local g_cache_bIsValid boolean, 
  	//to prevent any issue with another thread calling another GamescopeWSILayer:: function which calls an xcb:: func 
  	//that might otherwise access g_cache while g_cache is being cleared at the end of GamescopeWSILayer::VkDeviceOverrides::QueuePresentKHR()
    public:
        Prefetcher() = delete;
        explicit Prefetcher(xcb_connection_t* connection, xcb_window_t window) {
            g_cache.q_tree = xcb_query_tree(connection, window);
            g_cache.geo = xcb_get_geometry(connection, window);
            g_cache.window = window;
            g_cache_bIsValid = true;
        }

        ~Prefetcher() {
            g_cache_bIsValid = false;
            if (std::get<0>(g_cache.cached_replies)) {
                free(std::get<0>(g_cache.cached_replies));
                std::get<0>(g_cache.cached_replies) = nullptr;
            }
            if (std::get<1>(g_cache.cached_replies)) {
                free(std::get<1>(g_cache.cached_replies));
                std::get<1>(g_cache.cached_replies) = nullptr;
            }
        }
  };
 
  static std::optional<xcb_atom_t> getAtom(xcb_connection_t* connection, std::string_view name) {
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, false, name.length(), name.data());
    auto reply = Reply<xcb_intern_atom_reply_t>{ xcb_intern_atom_reply(connection, cookie, nullptr) };
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

    xcb_get_property_cookie_t cookie = xcb_get_property(connection, false, screen->root, atom, XCB_ATOM_CARDINAL, 0, sizeof(T) / sizeof(uint32_t));
    auto reply = Reply<xcb_get_property_reply_t>{ xcb_get_property_reply(connection, cookie, nullptr) };
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
      const bool tryCached = (g_cache_bIsValid && g_cache.window == window);

      xcb_query_tree_cookie_t cookie = tryCached ? g_cache.q_tree : xcb_query_tree(connection, window);

      auto reply = tryCached ? CachedReply(connection, cookie) : Reply<xcb_query_tree_reply_t>{ xcb_query_tree_reply(connection, cookie, nullptr) };

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
    const bool tryCached = (g_cache_bIsValid && g_cache.window == window);

    xcb_get_geometry_cookie_t cookie = tryCached ? g_cache.geo : xcb_get_geometry(connection, window);

    auto reply = tryCached ? CachedReply(connection, cookie) : Reply<xcb_get_geometry_reply_t>{ xcb_get_geometry_reply(connection, cookie, nullptr) };
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

    const bool tryCached = (g_cache_bIsValid && g_cache.window == window);

    xcb_query_tree_cookie_t cookie = tryCached ? g_cache.q_tree : xcb_query_tree(connection, window);

    auto reply = tryCached ? CachedReply(connection, cookie) : Reply<xcb_query_tree_reply_t>{ xcb_query_tree_reply(connection, cookie, nullptr) };

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

      xcb_get_window_attributes_cookie_t attributeCookie = xcb_get_window_attributes(connection, child);
      auto attributeReply = Reply<xcb_get_window_attributes_reply_t>{ xcb_get_window_attributes_reply(connection, attributeCookie, nullptr) };

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
