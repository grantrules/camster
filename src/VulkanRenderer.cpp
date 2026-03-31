#include "VulkanRenderer.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <sstream>

#ifdef __linux__
#include <unistd.h>
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include "StlMesh.hpp"

namespace {
// Device extensions required by this renderer. Keep this list minimal and
// additive so portability/debugging stay manageable.
const std::vector<const char*> kRequiredDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

// Validation layers are optional at runtime; they are enabled in debug builds
// when available and silently disabled otherwise.
const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

// VK_EXT_debug_utils functions are not loaded by default because they are
// extension entry points.  We look them up manually via vkGetInstanceProcAddr.
VkResult createDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    const VkAllocationCallbacks* allocator, VkDebugUtilsMessengerEXT* messenger) {
  auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  if (!fn) {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
  return fn(instance, createInfo, allocator, messenger);
}

void destroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                                   const VkAllocationCallbacks* allocator) {
  auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
  if (fn) {
    fn(instance, messenger, allocator);
  }
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
  // Prefer SRGB output when supported so colors authored in SRGB look correct.
  for (const auto& fmt : formats) {
    if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB &&
        fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return fmt;
    }
  }
  return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
  // MAILBOX gives low-latency triple-buffer behavior when available.
  // FIFO is guaranteed by spec and acts like VSync fallback.
  for (const auto& mode : modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return mode;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseSwapExtent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& capabilities) {
  // If the surface fixes the extent (common on some platforms), use it.
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);

  VkExtent2D extent{};
  extent.width =
      std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width,
                 capabilities.maxImageExtent.width);
  extent.height =
      std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height,
                 capabilities.maxImageExtent.height);
  return extent;
}

// Vertex input descriptions tell the pipeline how to interpret the raw byte
// stream in a vertex buffer.  The binding describes stride (bytes per vertex),
// and each attribute maps a shader input location to an offset inside that stride.
VkVertexInputBindingDescription vertexBinding() {
  VkVertexInputBindingDescription desc{};
  desc.binding = 0;
  desc.stride = sizeof(StlVertex);
  desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  return desc;
}

std::array<VkVertexInputAttributeDescription, 2> vertexAttributes() {
  std::array<VkVertexInputAttributeDescription, 2> attrs{};

  attrs[0].binding = 0;
  attrs[0].location = 0;
  attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset = offsetof(StlVertex, position);

  attrs[1].binding = 0;
  attrs[1].location = 1;
  attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[1].offset = offsetof(StlVertex, normal);

  return attrs;
}

std::filesystem::path executableDir() {
#ifdef __linux__
  std::array<char, 4096> buffer{};
  const ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (len > 0) {
    buffer[static_cast<size_t>(len)] = '\0';
    return std::filesystem::path(buffer.data()).parent_path();
  }
#endif
  return {};
}

std::filesystem::path findShaderDir(std::string* diagnostic) {
  // Runtime search keeps binaries portable across dev/build/install layouts.
  const std::filesystem::path exeDir = executableDir();
  const std::filesystem::path cwd = std::filesystem::current_path();

  const std::vector<std::filesystem::path> candidates = {
      std::filesystem::path(CAMSTER_SHADER_DIR),
      cwd / "shaders",
      cwd / "build" / "shaders",
      cwd / "build-debug" / "shaders",
      cwd / "build-debug-docker" / "shaders",
      cwd / "build-release-docker" / "shaders",
      exeDir / "shaders",
      exeDir.parent_path() / "shaders",
  };

  std::ostringstream attempted;
  for (const auto& candidate : candidates) {
    const auto vert = candidate / "mesh.vert.spv";
    const auto frag = candidate / "mesh.frag.spv";
    if (std::filesystem::exists(vert) && std::filesystem::exists(frag)) {
      return candidate;
    }
    attempted << "  - " << candidate.string() << "\n";
  }

  if (diagnostic) {
    *diagnostic = attempted.str();
  }
  return {};
}
}  // namespace

bool VulkanRenderer::QueueFamilyIndices::complete() const {
  return graphics != UINT32_MAX && present != UINT32_MAX;
}

VulkanRenderer::~VulkanRenderer() { cleanup(); }

bool VulkanRenderer::initialize(GLFWwindow* window, std::string& error) {
  // Initialization order tracks Vulkan object dependencies.
  // A later step can assume all earlier prerequisites exist.
  //
  // Instance -> Surface -> PhysicalDevice -> LogicalDevice -> Swapchain
  // -> ImageViews -> RenderPass -> Pipeline -> Buffers/Descriptors
  // -> Command infrastructure -> Sync -> ImGui backend.
  window_ = window;
  validationEnabled_ = CAMSTER_ENABLE_VALIDATION != 0;

  if (!createInstance(error) || !setupDebugMessenger(error) || !createSurface(error) ||
      !pickPhysicalDevice(error) ||
      !createLogicalDevice(error) || !createSwapchain(error) || !createImageViews(error) ||
      !createRenderPass(error) || !createDescriptorSetLayout(error) ||
      !createGraphicsPipeline(error) || !createCommandPool(error) || !createDepthResources(error) ||
      !createFramebuffers(error) || !createUniformBuffers(error) || !createDescriptorPool(error) ||
      !createDescriptorSets(error) || !createCommandBuffers(error) || !createSyncObjects(error) ||
      !initImGui(error)) {
    cleanup();
    return false;
  }

  return true;
}

// Teardown in roughly reverse creation order.  Wait for the device to be idle
// first so no in-flight work references objects we are about to destroy.
void VulkanRenderer::cleanup() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }

  if (imguiInitialized_) {
    if (ImGui::GetCurrentContext() != nullptr) {
      ImGuiIO& io = ImGui::GetIO();
      if (io.BackendRendererUserData != nullptr) {
        ImGui_ImplVulkan_Shutdown();
      }
      if (io.BackendPlatformUserData != nullptr) {
        ImGui_ImplGlfw_Shutdown();
      }
      ImGui::DestroyContext();
    }
    imguiInitialized_ = false;
  }

  destroyBuffer(vertexBuffer_);
  destroyBuffer(indexBuffer_);

  for (auto& ub : uniformBuffers_) {
    destroyBuffer(ub);
  }
  uniformBuffers_.clear();

  for (auto& frame : syncObjects_) {
    if (frame.imageAvailable) {
      vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
    }
    if (frame.renderFinished) {
      vkDestroySemaphore(device_, frame.renderFinished, nullptr);
    }
    if (frame.inFlight) {
      vkDestroyFence(device_, frame.inFlight, nullptr);
    }
  }
  syncObjects_.clear();

  if (descriptorPool_) {
    vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    descriptorPool_ = VK_NULL_HANDLE;
  }

  if (descriptorSetLayout_) {
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
    descriptorSetLayout_ = VK_NULL_HANDLE;
  }

  cleanupSwapchain();

  if (commandPool_) {
    vkDestroyCommandPool(device_, commandPool_, nullptr);
    commandPool_ = VK_NULL_HANDLE;
  }

  if (device_) {
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }

  if (surface_) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }

  if (instance_) {
    if (debugMessenger_) {
      destroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
      debugMessenger_ = VK_NULL_HANDLE;
    }
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

void VulkanRenderer::waitIdle() {
  if (device_) {
    vkDeviceWaitIdle(device_);
  }
}

void VulkanRenderer::markFramebufferResized() { framebufferResized_ = true; }

float VulkanRenderer::framebufferAspect() const {
  if (swapchainExtent_.height == 0) {
    return 1.0f;
  }
  return static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
}

void VulkanRenderer::beginImGuiFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void VulkanRenderer::toggleWireframe() {
  if (supportsWireframe_) {
    wireframeEnabled_ = !wireframeEnabled_;
  }
}

void VulkanRenderer::toggleNormalVisualization() {
  normalVisualizationEnabled_ = !normalVisualizationEnabled_;
}

bool VulkanRenderer::wireframeEnabled() const { return wireframeEnabled_; }

bool VulkanRenderer::normalVisualizationEnabled() const { return normalVisualizationEnabled_; }

bool VulkanRenderer::wireframeSupported() const { return supportsWireframe_; }

bool VulkanRenderer::validationEnabled() const { return validationEnabled_; }

bool VulkanRenderer::checkValidationLayerSupport() const {
  uint32_t layerCount = 0;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
  std::vector<VkLayerProperties> layers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

  for (const char* required : kValidationLayers) {
    bool found = false;
    for (const auto& layer : layers) {
      if (std::strcmp(required, layer.layerName) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }

  return true;
}

std::vector<const char*> VulkanRenderer::getRequiredExtensions() const {
  uint32_t glfwExtensionCount = 0;
  const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
  if (validationEnabled_) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  return extensions;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void*) {
  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    std::cerr << "[Vulkan] " << callbackData->pMessage << "\n";
  }
  return VK_FALSE;
}

bool VulkanRenderer::setupDebugMessenger(std::string& error) {
  if (!validationEnabled_) {
    return true;
  }

  VkDebugUtilsMessengerCreateInfoEXT createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;

  if (createDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_) !=
      VK_SUCCESS) {
    error = "Failed to create debug messenger.";
    return false;
  }
  return true;
}

void VulkanRenderer::setMesh(const StlMesh& mesh) {
  std::string ignored;
  uploadMeshBuffers(mesh, ignored);
}

bool VulkanRenderer::drawFrame(const glm::mat4& view, const glm::mat4& projection,
                               ImDrawData* drawData, std::string& error) {
  // 1) CPU waits until this frame slot is no longer in use by the GPU.
  //    This is the canonical "frames in flight" throttle.
  vkWaitForFences(device_, 1, &syncObjects_[currentFrame_].inFlight, VK_TRUE, UINT64_MAX);

  uint32_t imageIndex = 0;
  // 2) Acquire a swapchain image and signal imageAvailable when ownership is ready.
  VkResult acquireResult = vkAcquireNextImageKHR(
      device_, swapchain_, UINT64_MAX, syncObjects_[currentFrame_].imageAvailable, VK_NULL_HANDLE,
      &imageIndex);

  if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
    return recreateSwapchain(error);
  }
  if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
    error = "vkAcquireNextImageKHR failed.";
    return false;
  }

  // 3) Fence becomes unsignaled here and will be signaled by queue submit below.
  vkResetFences(device_, 1, &syncObjects_[currentFrame_].inFlight);

  // 4) Command buffer is rebuilt every frame because camera/UI state changes.
  //    Per-frame resources (command buffer, uniform buffer, descriptor set) are
  //    indexed by currentFrame_, NOT imageIndex, so the fence wait above
  //    guarantees they are no longer in use by the GPU.
  vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);

  if (!updateUniformBuffer(currentFrame_, view, projection, error) ||
      !recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, drawData, error)) {
    return false;
  }

  VkSemaphore waitSemaphores[] = {syncObjects_[currentFrame_].imageAvailable};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSemaphore signalSemaphores[] = {syncObjects_[currentFrame_].renderFinished};

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  // 5) Submit graphics work:
  //    wait: imageAvailable at color-attachment stage
  //    signal: renderFinished for presentation queue wait
  if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, syncObjects_[currentFrame_].inFlight) !=
      VK_SUCCESS) {
    error = "vkQueueSubmit failed.";
    return false;
  }

  VkSwapchainKHR swapchains[] = {swapchain_};
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapchains;
  presentInfo.pImageIndices = &imageIndex;

  // 6) Present waits on renderFinished so scanout never sees incomplete output.
  VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
  if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR ||
      framebufferResized_) {
    framebufferResized_ = false;
    if (!recreateSwapchain(error)) {
      return false;
    }
  } else if (presentResult != VK_SUCCESS) {
    error = "vkQueuePresentKHR failed.";
    return false;
  }

  currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;
  return true;
}

bool VulkanRenderer::createInstance(std::string& error) {
  if (validationEnabled_ && !checkValidationLayerSupport()) {
    validationEnabled_ = false;
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "camster";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "camster";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_1;

  const std::vector<const char*> extensions = getRequiredExtensions();

  // Chaining a debug messenger into pNext of the instance create info lets us
  // catch validation errors that occur during vkCreateInstance itself (which is
  // before the "real" debug messenger exists).
  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  if (validationEnabled_) {
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;
  }

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();
  if (validationEnabled_) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
    createInfo.ppEnabledLayerNames = kValidationLayers.data();
    createInfo.pNext = &debugCreateInfo;
  }

  if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
    error = "vkCreateInstance failed.";
    return false;
  }

  return true;
}

bool VulkanRenderer::createSurface(std::string& error) {
  if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
    error = "glfwCreateWindowSurface failed.";
    return false;
  }
  return true;
}

VulkanRenderer::QueueFamilyIndices VulkanRenderer::findQueueFamilies(VkPhysicalDevice device) const {
  QueueFamilyIndices indices;

  uint32_t familyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
  std::vector<VkQueueFamilyProperties> families(familyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

  for (uint32_t i = 0; i < familyCount; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphics = i;
    }

    VkBool32 supportsPresent = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &supportsPresent);
    if (supportsPresent) {
      indices.present = i;
    }

    if (indices.complete()) {
      break;
    }
  }

  return indices;
}

bool VulkanRenderer::pickPhysicalDevice(std::string& error) {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
  if (deviceCount == 0) {
    error = "No Vulkan-compatible GPU found.";
    return false;
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

  for (VkPhysicalDevice candidate : devices) {
    QueueFamilyIndices families = findQueueFamilies(candidate);
    if (!families.complete()) {
      continue;
    }

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data());

    std::set<std::string> missing(kRequiredDeviceExtensions.begin(), kRequiredDeviceExtensions.end());
    for (const auto& ext : extensions) {
      missing.erase(ext.extensionName);
    }
    if (!missing.empty()) {
      continue;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface_, &formatCount, nullptr);
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface_, &presentModeCount, nullptr);

    if (formatCount == 0 || presentModeCount == 0) {
      continue;
    }

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(candidate, &features);

    physicalDevice_ = candidate;
    queueFamilies_ = families;
    supportsWireframe_ = features.fillModeNonSolid == VK_TRUE;
    return true;
  }

  error = "No suitable Vulkan GPU with swapchain support found.";
  return false;
}

bool VulkanRenderer::createLogicalDevice(std::string& error) {
  std::set<uint32_t> uniqueFamilies = {queueFamilies_.graphics, queueFamilies_.present};

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  float priority = 1.0f;
  for (uint32_t family : uniqueFamilies) {
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    queueCreateInfos.push_back(queueInfo);
  }

  VkPhysicalDeviceFeatures features{};
  features.fillModeNonSolid = supportsWireframe_ ? VK_TRUE : VK_FALSE;

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  createInfo.pEnabledFeatures = &features;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(kRequiredDeviceExtensions.size());
  createInfo.ppEnabledExtensionNames = kRequiredDeviceExtensions.data();

  if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
    error = "vkCreateDevice failed.";
    return false;
  }

  vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
  vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);
  return true;
}

bool VulkanRenderer::createSwapchain(std::string& error) {
  VkSurfaceCapabilitiesKHR capabilities{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities);

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());

  uint32_t presentModeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr);
  std::vector<VkPresentModeKHR> presentModes(presentModeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount,
                                            presentModes.data());

  const VkSurfaceFormatKHR format = chooseSurfaceFormat(formats);
  const VkPresentModeKHR presentMode = choosePresentMode(presentModes);
  const VkExtent2D extent = chooseSwapExtent(window_, capabilities);

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface_;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = format.format;
  createInfo.imageColorSpace = format.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  // If graphics and present queues are different families, images must be
  // shared between them.  CONCURRENT is simpler but slightly less optimal than
  // explicit ownership transfers with EXCLUSIVE.
  const uint32_t queueFamilyIndices[] = {queueFamilies_.graphics, queueFamilies_.present};
  if (queueFamilies_.graphics != queueFamilies_.present) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;

  if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) != VK_SUCCESS) {
    error = "vkCreateSwapchainKHR failed.";
    return false;
  }

  vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
  swapchainImages_.resize(imageCount);
  vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());

  swapchainFormat_ = format.format;
  swapchainExtent_ = extent;
  return true;
}

bool VulkanRenderer::createImageViews(std::string& error) {
  swapchainImageViews_.resize(swapchainImages_.size());

  for (size_t i = 0; i < swapchainImages_.size(); ++i) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = swapchainImages_[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = swapchainFormat_;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &createInfo, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS) {
      error = "vkCreateImageView failed.";
      return false;
    }
  }

  return true;
}

bool VulkanRenderer::createRenderPass(std::string& error) {
  depthFormat_ = findDepthFormat();

  // Attachment 0: swapchain color target.
  // UNDEFINED -> PRESENT is valid because we clear each frame and only care
  // about final presentable image content.
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = swapchainFormat_;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  // Attachment 1: depth buffer.
  // Final layout remains depth-stencil optimal because it is not presented.
  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = depthFormat_;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthRef{};
  depthRef.attachment = 1;
  depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;
  subpass.pDepthStencilAttachment = &depthRef;

  // External -> subpass dependency performs implicit layout/visibility sync for
  // color and depth writes at the start of the render pass.
  // See Vulkan spec chapter on render pass synchronization.
  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

  VkRenderPassCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  createInfo.pAttachments = attachments.data();
  createInfo.subpassCount = 1;
  createInfo.pSubpasses = &subpass;
  createInfo.dependencyCount = 1;
  createInfo.pDependencies = &dependency;

  if (vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_) != VK_SUCCESS) {
    error = "vkCreateRenderPass failed.";
    return false;
  }

  return true;
}

bool VulkanRenderer::createDescriptorSetLayout(std::string& error) {
  VkDescriptorSetLayoutBinding uboBinding{};
  uboBinding.binding = 0;
  uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboBinding.descriptorCount = 1;
  uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  createInfo.bindingCount = 1;
  createInfo.pBindings = &uboBinding;

  if (vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &descriptorSetLayout_) !=
      VK_SUCCESS) {
    error = "vkCreateDescriptorSetLayout failed.";
    return false;
  }

  return true;
}

std::vector<char> VulkanRenderer::readBinaryFile(const std::string& path, std::string& error) const {
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file) {
    error = "Failed to open shader: " + path;
    return {};
  }

  const size_t size = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(size);
  file.seekg(0);
  file.read(buffer.data(), static_cast<std::streamsize>(size));
  return buffer;
}

VkShaderModule VulkanRenderer::createShaderModule(const std::vector<char>& code, std::string& error) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device_, &createInfo, nullptr, &module) != VK_SUCCESS) {
    error = "vkCreateShaderModule failed.";
  }
  return module;
}

bool VulkanRenderer::createGraphicsPipeline(std::string& error) {
  std::string shaderProbe;
  const std::filesystem::path shaderDir = findShaderDir(&shaderProbe);
  if (shaderDir.empty()) {
    error = "Could not locate shader directory. Tried:\n" + shaderProbe;
    return false;
  }

  const auto vertCode = readBinaryFile((shaderDir / "mesh.vert.spv").string(), error);
  if (vertCode.empty()) {
    return false;
  }
  const auto fragCode = readBinaryFile((shaderDir / "mesh.frag.spv").string(), error);
  if (fragCode.empty()) {
    return false;
  }

  VkShaderModule vert = createShaderModule(vertCode, error);
  if (!vert) {
    return false;
  }
  VkShaderModule frag = createShaderModule(fragCode, error);
  if (!frag) {
    vkDestroyShaderModule(device_, vert, nullptr);
    return false;
  }

  // This pipeline is mostly static-state for clarity.
  // Dynamic states (viewport/scissor, etc.) can be introduced later once
  // students are comfortable with baseline pipeline creation.
  VkPipelineShaderStageCreateInfo vertStage{};
  vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertStage.module = vert;
  vertStage.pName = "main";

  VkPipelineShaderStageCreateInfo fragStage{};
  fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragStage.module = frag;
  fragStage.pName = "main";

  VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

  const VkVertexInputBindingDescription binding = vertexBinding();
  const auto attrs = vertexAttributes();

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &binding;
  vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
  vertexInput.pVertexAttributeDescriptions = attrs.data();

  VkPipelineInputAssemblyStateCreateInfo assembly{};
  assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkViewport viewport{};
  viewport.width = static_cast<float>(swapchainExtent_.width);
  viewport.height = static_cast<float>(swapchainExtent_.height);
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.extent = swapchainExtent_;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  // Back-face culling assumes consistent CCW winding from mesh import.
  // If imports contain mixed winding, this is one of the first knobs to inspect.
  raster.cullMode = VK_CULL_MODE_BACK_BIT;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = 1;
  colorBlend.pAttachments = &colorBlendAttachment;

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &descriptorSetLayout_;

  if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
    error = "vkCreatePipelineLayout failed.";
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return false;
  }

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = stages;
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &assembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pMultisampleState = &multisample;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlend;
  pipelineInfo.layout = pipelineLayout_;
  pipelineInfo.renderPass = renderPass_;
  pipelineInfo.subpass = 0;

  VkPipelineRasterizationStateCreateInfo fillRaster = raster;
  fillRaster.polygonMode = VK_POLYGON_MODE_FILL;
  pipelineInfo.pRasterizationState = &fillRaster;

  if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                &fillPipeline_) != VK_SUCCESS) {
    error = "vkCreateGraphicsPipelines (fill) failed.";
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return false;
  }

  if (supportsWireframe_) {
    // Optional second pipeline for wireframe mode.
    // Vulkan exposes line rasterization through polygonMode=LINE, but support
    // is feature-gated (fillModeNonSolid).
    VkPipelineRasterizationStateCreateInfo wireRaster = raster;
    wireRaster.polygonMode = VK_POLYGON_MODE_LINE;
    wireRaster.lineWidth = 1.0f;
    pipelineInfo.pRasterizationState = &wireRaster;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &wireframePipeline_) != VK_SUCCESS) {
      wireframePipeline_ = VK_NULL_HANDLE;
    }
  }

  vkDestroyShaderModule(device_, vert, nullptr);
  vkDestroyShaderModule(device_, frag, nullptr);
  return true;
}

bool VulkanRenderer::createDepthResources(std::string& error) {
  if (!createImage(swapchainExtent_, depthFormat_, VK_IMAGE_TILING_OPTIMAL,
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   depthImage_, error)) {
    return false;
  }

  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  if (hasStencilComponent(depthFormat_)) {
    aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  depthImage_.view = createImageView(depthImage_.image, depthFormat_, aspect, error);
  return depthImage_.view != VK_NULL_HANDLE;
}

bool VulkanRenderer::createFramebuffers(std::string& error) {
  framebuffers_.resize(swapchainImageViews_.size());

  for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
    std::array<VkImageView, 2> attachments = {
        swapchainImageViews_[i],
        depthImage_.view,
    };

    VkFramebufferCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    createInfo.renderPass = renderPass_;
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.width = swapchainExtent_.width;
    createInfo.height = swapchainExtent_.height;
    createInfo.layers = 1;

    if (vkCreateFramebuffer(device_, &createInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
      error = "vkCreateFramebuffer failed.";
      return false;
    }
  }

  return true;
}

bool VulkanRenderer::createCommandPool(std::string& error) {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = queueFamilies_.graphics;
  // RESET_COMMAND_BUFFER_BIT: we re-record individual command buffers each frame.
  // TRANSIENT_BIT: hints the driver that buffers are short-lived, which may help
  // internal allocation strategies on some implementations.
  poolInfo.flags =
      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

  if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
    error = "vkCreateCommandPool failed.";
    return false;
  }

  return true;
}

uint32_t VulkanRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
  // Vulkan exposes memory heaps/types per physical device.
  // typeFilter comes from resource memory requirements; we select a type that
  // satisfies both compatibility bits and required property flags.
  VkPhysicalDeviceMemoryProperties memProps{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);

  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((typeFilter & (1u << i)) &&
        (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("Failed to find suitable memory type.");
}

VkFormat VulkanRenderer::findSupportedFormat(const std::vector<VkFormat>& candidates,
                                             VkImageTiling tiling,
                                             VkFormatFeatureFlags features) const {
  for (VkFormat format : candidates) {
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);

    if (tiling == VK_IMAGE_TILING_LINEAR &&
        (props.linearTilingFeatures & features) == features) {
      return format;
    }
    if (tiling == VK_IMAGE_TILING_OPTIMAL &&
        (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }

  throw std::runtime_error("No supported format found.");
}

VkFormat VulkanRenderer::findDepthFormat() const {
  return findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                              VK_FORMAT_D24_UNORM_S8_UINT},
                             VK_IMAGE_TILING_OPTIMAL,
                             VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

bool VulkanRenderer::hasStencilComponent(VkFormat format) const {
  return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties, Buffer& outBuffer,
                                  std::string& error) {
  // Buffer object + explicit memory allocation are separate in Vulkan.
  // This is deliberate: apps control placement trade-offs explicitly.
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device_, &bufferInfo, nullptr, &outBuffer.buffer) != VK_SUCCESS) {
    error = "vkCreateBuffer failed.";
    return false;
  }

  VkMemoryRequirements memReq{};
  vkGetBufferMemoryRequirements(device_, outBuffer.buffer, &memReq);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  try {
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);
  } catch (const std::exception& ex) {
    error = ex.what();
    vkDestroyBuffer(device_, outBuffer.buffer, nullptr);
    outBuffer.buffer = VK_NULL_HANDLE;
    return false;
  }

  if (vkAllocateMemory(device_, &allocInfo, nullptr, &outBuffer.memory) != VK_SUCCESS) {
    error = "vkAllocateMemory failed.";
    vkDestroyBuffer(device_, outBuffer.buffer, nullptr);
    outBuffer.buffer = VK_NULL_HANDLE;
    return false;
  }

  vkBindBufferMemory(device_, outBuffer.buffer, outBuffer.memory, 0);
  outBuffer.size = size;
  return true;
}

bool VulkanRenderer::createImage(VkExtent2D extent, VkFormat format, VkImageTiling tiling,
                                 VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                                 Image& outImage, std::string& error) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device_, &imageInfo, nullptr, &outImage.image) != VK_SUCCESS) {
    error = "vkCreateImage failed.";
    return false;
  }

  VkMemoryRequirements memReq{};
  vkGetImageMemoryRequirements(device_, outImage.image, &memReq);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  try {
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);
  } catch (const std::exception& ex) {
    error = ex.what();
    vkDestroyImage(device_, outImage.image, nullptr);
    outImage.image = VK_NULL_HANDLE;
    return false;
  }

  if (vkAllocateMemory(device_, &allocInfo, nullptr, &outImage.memory) != VK_SUCCESS) {
    error = "vkAllocateMemory for image failed.";
    vkDestroyImage(device_, outImage.image, nullptr);
    outImage.image = VK_NULL_HANDLE;
    return false;
  }

  if (vkBindImageMemory(device_, outImage.image, outImage.memory, 0) != VK_SUCCESS) {
    error = "vkBindImageMemory failed.";
    vkFreeMemory(device_, outImage.memory, nullptr);
    vkDestroyImage(device_, outImage.image, nullptr);
    outImage.memory = VK_NULL_HANDLE;
    outImage.image = VK_NULL_HANDLE;
    return false;
  }

  return true;
}

VkImageView VulkanRenderer::createImageView(VkImage image, VkFormat format,
                                            VkImageAspectFlags aspect, std::string& error) {
  VkImageViewCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  createInfo.image = image;
  createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  createInfo.format = format;
  createInfo.subresourceRange.aspectMask = aspect;
  createInfo.subresourceRange.baseMipLevel = 0;
  createInfo.subresourceRange.levelCount = 1;
  createInfo.subresourceRange.baseArrayLayer = 0;
  createInfo.subresourceRange.layerCount = 1;

  VkImageView view = VK_NULL_HANDLE;
  if (vkCreateImageView(device_, &createInfo, nullptr, &view) != VK_SUCCESS) {
    error = "vkCreateImageView for image failed.";
  }
  return view;
}

bool VulkanRenderer::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size, std::string& error) {
  // One-time submit helper used for staging transfers.
  // Simplicity over throughput: queue idle wait is acceptable at mesh-load time.
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool_;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (vkAllocateCommandBuffers(device_, &allocInfo, &cmd) != VK_SUCCESS) {
    error = "Failed to allocate copy command buffer.";
    return false;
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(cmd, &beginInfo);

  VkBufferCopy region{};
  region.size = size;
  vkCmdCopyBuffer(cmd, src, dst, 1, &region);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
    error = "Failed to submit copy command buffer.";
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    return false;
  }

  vkQueueWaitIdle(graphicsQueue_);
  vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
  return true;
}

void VulkanRenderer::destroyBuffer(Buffer& buffer) {
  if (buffer.buffer) {
    vkDestroyBuffer(device_, buffer.buffer, nullptr);
    buffer.buffer = VK_NULL_HANDLE;
  }
  if (buffer.memory) {
    vkFreeMemory(device_, buffer.memory, nullptr);
    buffer.memory = VK_NULL_HANDLE;
  }
  buffer.size = 0;
}

void VulkanRenderer::destroyImage(Image& image) {
  if (image.view) {
    vkDestroyImageView(device_, image.view, nullptr);
    image.view = VK_NULL_HANDLE;
  }
  if (image.image) {
    vkDestroyImage(device_, image.image, nullptr);
    image.image = VK_NULL_HANDLE;
  }
  if (image.memory) {
    vkFreeMemory(device_, image.memory, nullptr);
    image.memory = VK_NULL_HANDLE;
  }
}

bool VulkanRenderer::createUniformBuffers(std::string& error) {
  uniformBuffers_.resize(kFramesInFlight);
  for (auto& ub : uniformBuffers_) {
    if (!createBuffer(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ub,
                      error)) {
      return false;
    }
  }
  return true;
}

bool VulkanRenderer::createDescriptorPool(std::string& error) {
  // Shared descriptor pool for both app descriptors and ImGui descriptors.
  // The generous counts avoid pool fragmentation/reallocation complexity in
  // this teaching codebase.
  std::array<VkDescriptorPoolSize, 11> poolSizes{};
  poolSizes[0] = {VK_DESCRIPTOR_TYPE_SAMPLER, 1024};
  poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024};
  poolSizes[2] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024};
  poolSizes[3] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024};
  poolSizes[4] = {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1024};
  poolSizes[5] = {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1024};
  poolSizes[6] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024};
  poolSizes[7] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024};
  poolSizes[8] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024};
  poolSizes[9] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1024};
  poolSizes[10] = {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1024};

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = 1024;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();

  if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
    error = "vkCreateDescriptorPool failed.";
    return false;
  }

  return true;
}

bool VulkanRenderer::createDescriptorSets(std::string& error) {
  if (!descriptorSets_.empty()) {
    vkFreeDescriptorSets(device_, descriptorPool_, static_cast<uint32_t>(descriptorSets_.size()),
                         descriptorSets_.data());
  }

  std::vector<VkDescriptorSetLayout> layouts(kFramesInFlight, descriptorSetLayout_);

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool_;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
  allocInfo.pSetLayouts = layouts.data();

  descriptorSets_.resize(kFramesInFlight);
  if (vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
    error = "vkAllocateDescriptorSets failed.";
    return false;
  }

  for (size_t i = 0; i < descriptorSets_.size(); ++i) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffers_[i].buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSets_[i];
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  }

  return true;
}

bool VulkanRenderer::createCommandBuffers(std::string& error) {
  if (!commandBuffers_.empty()) {
    vkFreeCommandBuffers(device_, commandPool_, static_cast<uint32_t>(commandBuffers_.size()),
                         commandBuffers_.data());
  }

  commandBuffers_.resize(kFramesInFlight);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool_;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

  if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
    error = "vkAllocateCommandBuffers failed.";
    return false;
  }

  return true;
}

bool VulkanRenderer::createSyncObjects(std::string& error) {
  syncObjects_.resize(kFramesInFlight);

  VkSemaphoreCreateInfo semInfo{};
  semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  // Fences start signaled so the very first vkWaitForFences in drawFrame()
  // returns immediately rather than blocking forever.
  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (auto& sync : syncObjects_) {
    if (vkCreateSemaphore(device_, &semInfo, nullptr, &sync.imageAvailable) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semInfo, nullptr, &sync.renderFinished) != VK_SUCCESS ||
        vkCreateFence(device_, &fenceInfo, nullptr, &sync.inFlight) != VK_SUCCESS) {
      error = "Failed to create sync objects.";
      return false;
    }
  }

  return true;
}

bool VulkanRenderer::uploadMeshBuffers(const StlMesh& mesh, std::string& error) {
  destroyBuffer(vertexBuffer_);
  destroyBuffer(indexBuffer_);

  indexCount_ = static_cast<uint32_t>(mesh.indices().size());
  if (mesh.empty()) {
    return true;
  }

  const VkDeviceSize vertexSize = sizeof(StlVertex) * mesh.vertices().size();
  const VkDeviceSize indexSize = sizeof(uint32_t) * mesh.indices().size();

  // Classic staging pattern:
  // 1) HOST_VISIBLE staging buffer (CPU writes)
  // 2) DEVICE_LOCAL vertex/index buffer (GPU-optimal)
  // 3) vkCmdCopyBuffer transfer
  //
  // Reference:
  // https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap11.html
  Buffer vertexStaging;
  if (!createBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    vertexStaging, error)) {
    return false;
  }

  void* mapped = nullptr;
  vkMapMemory(device_, vertexStaging.memory, 0, vertexSize, 0, &mapped);
  std::memcpy(mapped, mesh.vertices().data(), static_cast<size_t>(vertexSize));
  vkUnmapMemory(device_, vertexStaging.memory);

  if (!createBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer_, error)) {
    destroyBuffer(vertexStaging);
    return false;
  }

  if (!copyBuffer(vertexStaging.buffer, vertexBuffer_.buffer, vertexSize, error)) {
    destroyBuffer(vertexStaging);
    return false;
  }
  destroyBuffer(vertexStaging);

  Buffer indexStaging;
  if (!createBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    indexStaging, error)) {
    return false;
  }

  vkMapMemory(device_, indexStaging.memory, 0, indexSize, 0, &mapped);
  std::memcpy(mapped, mesh.indices().data(), static_cast<size_t>(indexSize));
  vkUnmapMemory(device_, indexStaging.memory);

  if (!createBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer_, error)) {
    destroyBuffer(indexStaging);
    return false;
  }

  if (!copyBuffer(indexStaging.buffer, indexBuffer_.buffer, indexSize, error)) {
    destroyBuffer(indexStaging);
    return false;
  }
  destroyBuffer(indexStaging);

  return true;
}

bool VulkanRenderer::updateUniformBuffer(uint32_t frameIndex, const glm::mat4& view,
                                         const glm::mat4& projection, std::string& error) {
  if (frameIndex >= uniformBuffers_.size()) {
    error = "Uniform buffer frame index out of bounds.";
    return false;
  }

  UniformBufferObject ubo{};
  ubo.model = glm::mat4(1.0f);
  ubo.view = view;
  ubo.projection = projection;
  ubo.options = glm::vec4(normalVisualizationEnabled_ ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);

  void* mapped = nullptr;
  vkMapMemory(device_, uniformBuffers_[frameIndex].memory, 0, sizeof(ubo), 0, &mapped);
  std::memcpy(mapped, &ubo, sizeof(ubo));
  vkUnmapMemory(device_, uniformBuffers_[frameIndex].memory);

  return true;
}

bool VulkanRenderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex,
                                         ImDrawData* drawData, std::string& error) {
  // Command buffers in Vulkan are explicit command streams.
  // We record one primary CB per frame-in-flight every frame.  The framebuffer
  // is selected by imageIndex (which swapchain image we acquired), while the CB
  // itself is indexed by currentFrame_ and protected by its matching fence.
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
    error = "vkBeginCommandBuffer failed.";
    return false;
  }

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = renderPass_;
  renderPassInfo.framebuffer = framebuffers_[imageIndex];
  renderPassInfo.renderArea.extent = swapchainExtent_;

  std::array<VkClearValue, 2> clearValues{};
  clearValues[0].color = {{0.07f, 0.08f, 0.10f, 1.0f}};
  clearValues[1].depthStencil = {1.0f, 0};
  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  if (indexCount_ > 0) {
    VkPipeline activePipeline = fillPipeline_;
    if (wireframeEnabled_ && wireframePipeline_ != VK_NULL_HANDLE) {
      activePipeline = wireframePipeline_;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);

    VkBuffer buffers[] = {vertexBuffer_.buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer_.buffer, 0, VK_INDEX_TYPE_UINT32);

    // Descriptor set is indexed by currentFrame_ to match per-frame UBOs.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                            &descriptorSets_[currentFrame_], 0, nullptr);

    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
  }

  ImGui_ImplVulkan_RenderDrawData(drawData, cmd);

  vkCmdEndRenderPass(cmd);

  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
    error = "vkEndCommandBuffer failed.";
    return false;
  }

  return true;
}

bool VulkanRenderer::initImGui(std::string& error) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  if (!ImGui_ImplGlfw_InitForVulkan(window_, true)) {
    error = "ImGui_ImplGlfw_InitForVulkan failed.";
    ImGui::DestroyContext();
    return false;
  }

  if (!initImGuiVulkanBackend(error)) {
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    return false;
  }

  imguiInitialized_ = true;
  return true;
}

bool VulkanRenderer::initImGuiVulkanBackend(std::string& error) {
  // ImGui backend consumes our existing Vulkan objects (device, queue, render
  // pass, descriptor pool) and records draw commands into our command buffers.
  // Backend docs: https://github.com/ocornut/imgui/tree/master/backends
  ImGui_ImplVulkan_InitInfo initInfo{};
  initInfo.Instance = instance_;
  initInfo.PhysicalDevice = physicalDevice_;
  initInfo.Device = device_;
  initInfo.QueueFamily = queueFamilies_.graphics;
  initInfo.Queue = graphicsQueue_;
  initInfo.PipelineCache = VK_NULL_HANDLE;
  initInfo.DescriptorPool = descriptorPool_;
  initInfo.RenderPass = renderPass_;
  initInfo.Allocator = nullptr;
  initInfo.MinImageCount = static_cast<uint32_t>(swapchainImages_.size());
  initInfo.ImageCount = static_cast<uint32_t>(swapchainImages_.size());
  initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  initInfo.CheckVkResultFn = nullptr;

  if (!ImGui_ImplVulkan_Init(&initInfo)) {
    error = "ImGui_ImplVulkan_Init failed.";
    return false;
  }

  if (!ImGui_ImplVulkan_CreateFontsTexture()) {
    error = "ImGui_ImplVulkan_CreateFontsTexture failed.";
    ImGui_ImplVulkan_Shutdown();
    return false;
  }

  ImGui_ImplVulkan_DestroyFontsTexture();
  return true;
}

// Destroy everything that depends on swapchain dimensions: framebuffers, depth
// image, pipelines (baked viewport/scissor), render pass, image views, and the
// swapchain itself.  Per-frame resources (UBOs, descriptor sets, command
// buffers, sync objects) are independent and survive.
void VulkanRenderer::cleanupSwapchain() {
  for (auto framebuffer : framebuffers_) {
    vkDestroyFramebuffer(device_, framebuffer, nullptr);
  }
  framebuffers_.clear();

  destroyImage(depthImage_);

  if (fillPipeline_) {
    vkDestroyPipeline(device_, fillPipeline_, nullptr);
    fillPipeline_ = VK_NULL_HANDLE;
  }

  if (wireframePipeline_) {
    vkDestroyPipeline(device_, wireframePipeline_, nullptr);
    wireframePipeline_ = VK_NULL_HANDLE;
  }

  if (pipelineLayout_) {
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    pipelineLayout_ = VK_NULL_HANDLE;
  }

  if (renderPass_) {
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    renderPass_ = VK_NULL_HANDLE;
  }

  for (auto imageView : swapchainImageViews_) {
    vkDestroyImageView(device_, imageView, nullptr);
  }
  swapchainImageViews_.clear();

  if (swapchain_) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

bool VulkanRenderer::recreateSwapchain(std::string& error) {
  // Minimized windows can report zero framebuffer dimensions.
  // Waiting avoids creating invalid zero-sized swapchain resources.
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window_, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(window_, &width, &height);
    glfwWaitEvents();
  }

  // Full-device idle keeps swapchain teardown/rebuild simple and correct.
  // Fine-grained overlap is possible but not needed for this project baseline.
  vkDeviceWaitIdle(device_);

  cleanupSwapchain();

  // Per-frame resources (uniform buffers, descriptor sets, command buffers)
  // are sized to kFramesInFlight and do not depend on swapchain image count,
  // so they survive swapchain recreation unchanged.

  if (!createSwapchain(error) || !createImageViews(error) || !createRenderPass(error) ||
      !createGraphicsPipeline(error) || !createDepthResources(error) || !createFramebuffers(error)) {
    return false;
  }

  if (ImGui::GetCurrentContext() != nullptr) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.BackendRendererUserData != nullptr) {
      ImGui_ImplVulkan_Shutdown();
    }
    if (!initImGuiVulkanBackend(error)) {
      return false;
    }
  }

  ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(swapchainImages_.size()));
  return true;
}
