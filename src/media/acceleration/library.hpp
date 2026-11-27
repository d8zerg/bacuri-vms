#pragma once
#include <dlfcn.h>
#include <stdexcept>

namespace bacuri::vms::acceleration
{
class Library
{
  public:
    explicit Library(const char* name) : handle_{dlopen(name, RTLD_NOW | RTLD_LOCAL)} {}
    ~Library()
    {
        if (handle_)
            dlclose(handle_);
    }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    explicit operator bool() const
    {
        return handle_ != nullptr;
    }
    template <typename Function> Function* symbol(const char* name) const
    {
        auto* address = handle_ ? dlsym(handle_, name) : nullptr;
        if (!address)
            throw std::runtime_error("Optional driver entry point unavailable");
        return reinterpret_cast<Function*>(address);
    }

  private:
    void* handle_;
};
} // namespace bacuri::vms::acceleration
