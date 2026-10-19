#include "VkCommon.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace realm::gfx::vk {
namespace {
// Baseline the backend needs from a device; others are not candidates.
constexpr VkDeviceSize min_storage_buffer_range = 8u << 20;  // walls: 262144 corners x 32 bytes
constexpr std::uint32_t min_image_dimension = 2048;          // one atlas page
constexpr std::uint32_t min_array_layers = 256;               // spec minimum
constexpr std::uint32_t min_push_constants = 128;             // spec minimum

// A Vulkan create-info with its sType set and every other member zero.
template <typename T>
T make_info(VkStructureType type) {
  T value{};
  value.sType = type;
  return value;
}

bool env_is(const char* name, std::string_view value) {
  const char* text = std::getenv(name);
  return text && std::string_view(text) == value;
}

bool has_extension(const std::vector<VkExtensionProperties>& list, const char* name) {
  return std::any_of(list.begin(), list.end(), [&](const VkExtensionProperties& e) {
    return std::strcmp(e.extensionName, name) == 0;
  });
}

std::vector<VkExtensionProperties> instance_extensions(const Dispatch& fn) {
  std::uint32_t count = 0;
  check(fn.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
        "vkEnumerateInstanceExtensionProperties");
  std::vector<VkExtensionProperties> list(count);
  check(fn.vkEnumerateInstanceExtensionProperties(nullptr, &count, list.data()),
        "vkEnumerateInstanceExtensionProperties");
  return list;
}

bool layer_present(const Dispatch& fn, const char* name) {
  std::uint32_t count = 0;
  fn.vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  fn.vkEnumerateInstanceLayerProperties(&count, layers.data());
  return std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties& l) {
    return std::strcmp(l.layerName, name) == 0;
  });
}

// Validation output: every error and the first warnings are printed; counts are kept for the
// summary at teardown.
VKAPI_ATTR VkBool32 VKAPI_CALL on_validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void* user) {
  auto* context = static_cast<Context*>(user);
  const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
  const bool warning = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0;
  if (!error && !warning) {
    return VK_FALSE;
  }
  const auto count = error ? ++context->validation_errors : ++context->validation_warnings;
  if (count <= 50) {
    std::fprintf(stderr, "[vulkan %s #%llu] %s\n", error ? "error" : "warning",
                 static_cast<unsigned long long>(count),
                 data && data->pMessage ? data->pMessage : "(no message)");
  }
  return VK_FALSE;
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

const char* type_name(VkPhysicalDeviceType type) {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
      return "cpu";
    default:
      return "other";
  }
}

// Rank of a usable device: discrete over integrated over virtual over CPU.
int type_rank(VkPhysicalDeviceType type) {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return 4;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return 3;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
      return 1;
    default:
      return 0;
  }
}

// Why a device cannot run the backend, or empty when it can. Fills the family it would use.
std::string unusable_reason(Context& context, VkPhysicalDevice device, std::uint32_t& family) {
  const auto& fn = context.fn;
  VkPhysicalDeviceProperties properties{};
  fn.vkGetPhysicalDeviceProperties(device, &properties);
  if (properties.apiVersion < VK_API_VERSION_1_1) {
    return "Vulkan 1.1 required";
  }
  std::uint32_t count = 0;
  fn.vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  fn.vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
  if (!has_extension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
    return "no " VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  }
  std::uint32_t families = 0;
  fn.vkGetPhysicalDeviceQueueFamilyProperties(device, &families, nullptr);
  std::vector<VkQueueFamilyProperties> queues(families);
  fn.vkGetPhysicalDeviceQueueFamilyProperties(device, &families, queues.data());
  bool found = false;
  for (std::uint32_t i = 0; i < families && !found; ++i) {
    VkBool32 present = VK_FALSE;
    fn.vkGetPhysicalDeviceSurfaceSupportKHR(device, i, context.surface, &present);
    if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present && queues[i].queueCount > 0) {
      family = i;
      found = true;
    }
  }
  if (!found) {
    return "no graphics queue that presents to the window";
  }
  std::uint32_t formats = 0;
  fn.vkGetPhysicalDeviceSurfaceFormatsKHR(device, context.surface, &formats, nullptr);
  std::vector<VkSurfaceFormatKHR> surface_formats(formats);
  fn.vkGetPhysicalDeviceSurfaceFormatsKHR(device, context.surface, &formats, surface_formats.data());
  const bool swapchain_format = std::any_of(
      surface_formats.begin(), surface_formats.end(), [](const VkSurfaceFormatKHR& f) {
        return f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM;
      });
  if (!swapchain_format) {
    return "no UNORM RGBA8 or BGRA8 swapchain format";
  }
  std::uint32_t modes = 0;
  fn.vkGetPhysicalDeviceSurfacePresentModesKHR(device, context.surface, &modes, nullptr);
  std::vector<VkPresentModeKHR> present_modes(modes);
  fn.vkGetPhysicalDeviceSurfacePresentModesKHR(device, context.surface, &modes,
                                               present_modes.data());
  if (std::find(present_modes.begin(), present_modes.end(), VK_PRESENT_MODE_FIFO_KHR) ==
      present_modes.end()) {
    return "no FIFO present mode";
  }
  VkFormatProperties format{};
  fn.vkGetPhysicalDeviceFormatProperties(device, VK_FORMAT_R8G8B8A8_UNORM, &format);
  const auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  if ((format.optimalTilingFeatures & required) != required) {
    return "RGBA8 lacks sampling, attachment or transfer support";
  }
  const auto& limits = properties.limits;
  if (limits.maxImageDimension2D < min_image_dimension ||
      limits.maxImageArrayLayers < min_array_layers ||
      limits.maxPushConstantsSize < min_push_constants ||
      limits.maxStorageBufferRange < min_storage_buffer_range) {
    return "limits below the atlas, wall or push-constant baseline";
  }
  return {};
}

}  // namespace

void Dispatch::load_global(PFN_vkGetInstanceProcAddr get_instance) {
#define REALM_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(get_instance(nullptr, #name));
  REALM_VK_GLOBAL_FUNCTIONS(REALM_VK_LOAD)
#undef REALM_VK_LOAD
}

void Dispatch::load_instance(PFN_vkGetInstanceProcAddr get_instance, VkInstance instance) {
#define REALM_VK_LOAD(name) \
  name = reinterpret_cast<PFN_##name>(get_instance(instance, #name));
  REALM_VK_INSTANCE_FUNCTIONS(REALM_VK_LOAD)
#undef REALM_VK_LOAD
}

void Dispatch::load_device(VkDevice device) {
#define REALM_VK_LOAD(name)                                                    \
  if (auto proc = vkGetDeviceProcAddr(device, #name)) {                        \
    name = reinterpret_cast<PFN_##name>(proc);                                 \
  }
  REALM_VK_DEVICE_FUNCTIONS(REALM_VK_LOAD)
#undef REALM_VK_LOAD
}

Context& context() {
  static Context instance;
  return instance;
}

Context::~Context() {
  destroy();
}

void Context::destroy() noexcept {
  if (device != VK_NULL_HANDLE) {
    fn.vkDeviceWaitIdle(device);
    fn.vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
  }
  if (surface != VK_NULL_HANDLE) {
    fn.vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
  }
  if (messenger != VK_NULL_HANDLE) {
    fn.vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
    messenger = VK_NULL_HANDLE;
  }
  if (instance != VK_NULL_HANDLE) {
    if (validation) {
      std::fprintf(stderr, "Vulkan validation summary: %llu errors, %llu warnings\n",
                   static_cast<unsigned long long>(validation_errors.load()),
                   static_cast<unsigned long long>(validation_warnings.load()));
    }
    fn.vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
  physical = VK_NULL_HANDLE;
  queue = VK_NULL_HANDLE;
}

void Context::initialise(const WindowSurface& window) {
  try {
    create_instance(window);
    const auto handle = window.create_surface(instance);
    if (!handle) {
      throw std::runtime_error("Vulkan surface could not be created for the window");
    }
    static_assert(sizeof(VkSurfaceKHR) <= sizeof(std::uint64_t));
    std::memcpy(&surface, &handle, sizeof(surface));
    select_device();
  } catch (...) {
    destroy();
    throw;
  }
}

void Context::create_instance(const WindowSurface& window) {
  const auto get_instance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(window.instance_proc());
  if (!get_instance) {
    throw std::runtime_error("Vulkan loader has no vkGetInstanceProcAddr");
  }
  fn.load_global(get_instance);
  if (!fn.vkCreateInstance || !fn.vkEnumerateInstanceExtensionProperties) {
    throw std::runtime_error("Vulkan loader lacks instance creation");
  }
  std::uint32_t version = VK_API_VERSION_1_0;
  if (fn.vkEnumerateInstanceVersion) {
    fn.vkEnumerateInstanceVersion(&version);
  }
  if (version < VK_API_VERSION_1_1) {
    throw std::runtime_error("Vulkan loader offers less than 1.1");
  }

  // The strings live in the list for the whole instance creation.
  const auto window_extensions = window.instance_extensions();
  std::vector<const char*> extensions;
  for (const auto& name : window_extensions) {
    extensions.push_back(name.c_str());
  }

  const auto available = instance_extensions(fn);
  validation = env_is("REALM_VK_VALIDATION", "1");
  std::vector<const char*> layers;
  bool sync_validation = false;
  if (validation) {
    if (has_extension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    if (has_extension(available, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
      extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
      sync_validation = true;
    }
    if (layer_present(fn, "VK_LAYER_KHRONOS_validation")) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
    } else {
      std::fprintf(stderr, "Vulkan validation requested but VK_LAYER_KHRONOS_validation is missing\n");
      validation = false;
    }
  }
  for (const char* name : extensions) {
    if (!has_extension(available, name)) {
      throw std::runtime_error(std::string("Vulkan instance extension missing: ") + name);
    }
  }

  auto application = make_info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
  application.pApplicationName = "Realm of the Mad God";
  application.apiVersion = VK_API_VERSION_1_1;

  auto messenger_info = make_info<VkDebugUtilsMessengerCreateInfoEXT>(
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
  messenger_info.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  messenger_info.pfnUserCallback = on_validation;
  messenger_info.pUserData = this;

  const VkValidationFeatureEnableEXT enabled_features[] = {
      VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
  auto features = make_info<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
  features.enabledValidationFeatureCount = 1;
  features.pEnabledValidationFeatures = enabled_features;
  // Instance creation chains the messenger (and sync validation); the messenger created afterwards
  // is a plain one, since validation features are only valid on the instance.
  VkDebugUtilsMessengerCreateInfoEXT instance_messenger = messenger_info;
  instance_messenger.pNext = sync_validation ? &features : nullptr;

  auto info = make_info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
  info.pApplicationInfo = &application;
  info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
  info.ppEnabledLayerNames = layers.data();
  info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
  info.ppEnabledExtensionNames = extensions.data();
  if (validation) {
    info.pNext = &instance_messenger;
  }
  check(fn.vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
  fn.load_instance(get_instance, instance);
  if (validation) {
    check(fn.vkCreateDebugUtilsMessengerEXT(instance, &messenger_info, nullptr, &messenger),
          "vkCreateDebugUtilsMessengerEXT");
  }
}

void Context::select_device() {
  std::uint32_t count = 0;
  check(fn.vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
  std::vector<VkPhysicalDevice> devices(count);
  check(fn.vkEnumeratePhysicalDevices(instance, &count, devices.data()),
        "vkEnumeratePhysicalDevices");

  const char* request = std::getenv("REALM_VK_DEVICE");
  const bool list = env_is("REALM_VK_LIST", "1");
  struct Candidate {
    VkPhysicalDevice device;
    std::uint32_t index;
    std::uint32_t family;
    std::string name;
    VkPhysicalDeviceType type;
    std::string reason;
  };
  std::vector<Candidate> candidates;
  for (std::uint32_t i = 0; i < devices.size(); ++i) {
    VkPhysicalDeviceProperties device_properties{};
    fn.vkGetPhysicalDeviceProperties(devices[i], &device_properties);
    std::uint32_t candidate_family = 0;
    auto reason = unusable_reason(*this, devices[i], candidate_family);
    candidates.push_back({devices[i], i, candidate_family, device_properties.deviceName,
                          device_properties.deviceType, std::move(reason)});
    if (list) {
      std::cout << "Vulkan device [" << i << "] " << device_properties.deviceName << " ("
                << type_name(device_properties.deviceType) << ", API "
                << VK_API_VERSION_MAJOR(device_properties.apiVersion) << '.'
                << VK_API_VERSION_MINOR(device_properties.apiVersion) << ") "
                << (candidates.back().reason.empty() ? std::string("usable")
                                                     : "unusable: " + candidates.back().reason)
                << '\n';
    }
  }

  const Candidate* chosen = nullptr;
  if (request && *request) {
    const std::string wanted = request;
    const bool by_index = std::all_of(wanted.begin(), wanted.end(),
                                      [](unsigned char c) { return std::isdigit(c) != 0; });
    for (const auto& candidate : candidates) {
      const bool match = by_index ? std::to_string(candidate.index) == wanted
                                  : lower(candidate.name).find(lower(wanted)) != std::string::npos;
      if (match) {
        if (!candidate.reason.empty()) {
          throw std::runtime_error("REALM_VK_DEVICE selects an unusable device: " + candidate.name +
                                   " (" + candidate.reason + ")");
        }
        chosen = &candidate;
        break;
      }
    }
    if (!chosen) {
      throw std::runtime_error(std::string("REALM_VK_DEVICE matches no Vulkan device: ") + wanted);
    }
  } else {
    for (const auto& candidate : candidates) {
      if (!candidate.reason.empty()) {
        continue;
      }
      if (!chosen || type_rank(candidate.type) > type_rank(chosen->type)) {
        chosen = &candidate;
      }
    }
    if (!chosen) {
      throw std::runtime_error("No Vulkan device meets the baseline (1.1, swapchain, RGBA8, limits)");
    }
  }

  physical = chosen->device;
  family = chosen->family;
  fn.vkGetPhysicalDeviceProperties(physical, &properties);
  fn.vkGetPhysicalDeviceMemoryProperties(physical, &memory);
  limits = properties.limits;
  std::uint32_t family_count = 0;
  fn.vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
  std::vector<VkQueueFamilyProperties> queues(family_count);
  fn.vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, queues.data());
  timestamp_bits = queues[family].timestampValidBits;
  timestamp_period_ns = limits.timestampPeriod;

  const float priority = 1.0f;
  auto queue_info = make_info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
  queue_info.queueFamilyIndex = family;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  auto device_info = make_info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.enabledExtensionCount = 1;
  device_info.ppEnabledExtensionNames = device_extensions;
  check(fn.vkCreateDevice(physical, &device_info, nullptr, &device), "vkCreateDevice");
  fn.load_device(device);
  fn.vkGetDeviceQueue(device, family, 0, &queue);
}

std::uint32_t Context::memory_type(std::uint32_t type_bits, VkMemoryPropertyFlags required) const {
  for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) &&
        (memory.memoryTypes[i].propertyFlags & required) == required) {
      return i;
    }
  }
  throw std::runtime_error("No Vulkan memory type has the required properties");
}

std::uint32_t Context::memory_type(std::uint32_t type_bits, VkMemoryPropertyFlags required,
                                   VkMemoryPropertyFlags preferred) const {
  for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) &&
        (memory.memoryTypes[i].propertyFlags & (required | preferred)) == (required | preferred)) {
      return i;
    }
  }
  return memory_type(type_bits, required);
}

GraphicsInfo Context::info() const {
  const auto vendor = [](std::uint32_t id) -> std::string {
    switch (id) {
      case 0x1002:
        return "AMD";
      case 0x10DE:
        return "NVIDIA";
      case 0x8086:
        return "Intel";
      case 0x13B5:
        return "ARM";
      case 0x5143:
        return "Qualcomm";
      case 0x1010:
        return "ImgTec";
      case 0x106B:
        return "Apple";
      case 0x10005:
        return "Mesa";
      default:
        return "vendor 0x" + std::to_string(id);
    }
  };
  const auto api = properties.apiVersion;
  // NVIDIA packs its driver version differently from the Vulkan version triple.
  const auto driver = properties.vendorID == 0x10DE
                          ? std::to_string(properties.driverVersion >> 22) + "." +
                                std::to_string((properties.driverVersion >> 14) & 0xff) + "." +
                                std::to_string(properties.driverVersion & 0x3fff)
                          : std::to_string(VK_API_VERSION_MAJOR(properties.driverVersion)) + "." +
                                std::to_string(VK_API_VERSION_MINOR(properties.driverVersion)) + "." +
                                std::to_string(VK_API_VERSION_PATCH(properties.driverVersion));
  return {vendor(properties.vendorID), properties.deviceName,
          "Vulkan " + std::to_string(VK_API_VERSION_MAJOR(api)) + "." +
              std::to_string(VK_API_VERSION_MINOR(api)) + " driver " + driver};
}
namespace {
Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
                     VkMemoryPropertyFlags preferred) {
  auto& device = context();
  Buffer result;
  result.size = size;
  auto info = make_info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
  info.size = size;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(device.fn.vkCreateBuffer(device.device, &info, nullptr, &result.buffer), "vkCreateBuffer");
  VkMemoryRequirements requirements{};
  device.fn.vkGetBufferMemoryRequirements(device.device, result.buffer, &requirements);
  auto allocation = make_info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex =
      device.memory_type(requirements.memoryTypeBits, required, preferred);
  if (device.fn.vkAllocateMemory(device.device, &allocation, nullptr, &result.memory) != VK_SUCCESS) {
    destroy_buffer(result);
    throw std::runtime_error("Vulkan buffer memory allocation failed");
  }
  check(device.fn.vkBindBufferMemory(device.device, result.buffer, result.memory, 0),
        "vkBindBufferMemory");
  if (required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    void* mapped = nullptr;
    check(device.fn.vkMapMemory(device.device, result.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
          "vkMapMemory");
    result.mapped = static_cast<std::byte*>(mapped);
  }
  return result;
}
}  // namespace

Buffer create_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage) {
  return create_buffer(size, usage,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       0);
}

Buffer create_device_buffer(VkDeviceSize size, VkBufferUsageFlags usage) {
  return create_buffer(size, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
}

void destroy_buffer(Buffer& buffer) noexcept {
  auto& device = context();
  if (buffer.buffer != VK_NULL_HANDLE) {
    device.fn.vkDestroyBuffer(device.device, buffer.buffer, nullptr);
  }
  if (buffer.memory != VK_NULL_HANDLE) {
    if (buffer.mapped) {
      device.fn.vkUnmapMemory(device.device, buffer.memory);
    }
    device.fn.vkFreeMemory(device.device, buffer.memory, nullptr);
  }
  buffer = Buffer{};
}

}  // namespace realm::gfx::vk
