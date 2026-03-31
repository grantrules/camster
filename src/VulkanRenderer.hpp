#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include "ColorVertex.hpp"

struct GLFWwindow;
struct ImDrawData;
class StlMesh;

// VulkanRenderer owns the complete Vulkan state machine for this app:
// instance/device/swapchain/pipeline/synchronization, plus ImGui integration.
//
// Design note for students:
// - The public API is intentionally small (initialize, drawFrame, setMesh, etc.).
// - Most learning value is in private helpers that mirror Vulkan object lifetimes.
// - This follows the "explicit ownership" style encouraged by Vulkan itself.
//
// Recommended reading:
// https://registry.khronos.org/vulkan/specs/1.3-extensions/html/
// https://vulkan-tutorial.com/
class VulkanRenderer {
 public:
  ~VulkanRenderer();

  VulkanRenderer() = default;
  VulkanRenderer(const VulkanRenderer&) = delete;
  VulkanRenderer& operator=(const VulkanRenderer&) = delete;
  VulkanRenderer(VulkanRenderer&&) = delete;
  VulkanRenderer& operator=(VulkanRenderer&&) = delete;

  bool initialize(GLFWwindow* window, std::string& error);
  void cleanup();

  bool drawFrame(const glm::mat4& view, const glm::mat4& projection, ImDrawData* drawData,
                 std::string& error);
  void beginImGuiFrame();
  void waitIdle();

  void setMesh(const StlMesh& mesh);
  void setLines(const std::vector<ColorVertex>& lines);
  void markFramebufferResized();
  void toggleWireframe();
  void toggleNormalVisualization();
  bool wireframeEnabled() const;
  bool normalVisualizationEnabled() const;
  bool wireframeSupported() const;
  bool validationEnabled() const;

  float framebufferAspect() const;

 private:
  // CPU-side handles for GPU buffer allocations.
  struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
  };

  // Image resource + memory + view bundled together for simpler ownership.
  struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
  };

  // Queue families are selected during physical-device suitability checks.
  // graphics: command recording/submit queue, present: swapchain present queue.
  struct QueueFamilyIndices {
    uint32_t graphics = UINT32_MAX;
    uint32_t present = UINT32_MAX;

    bool complete() const;
  };

  // Per-frame synchronization primitives for "frames in flight":
  // imageAvailable -> GPU can render to acquired image
  // renderFinished -> presentation waits until rendering completes
  // inFlight -> CPU throttle so we do not overwrite in-use frame resources
  struct FrameSync {
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
  };

  // Matches uniform block layout used by mesh shaders.
  // options.x currently toggles normals visualization in the fragment shader.
  struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec4 options;
  };

  bool createInstance(std::string& error);
  bool setupDebugMessenger(std::string& error);
  bool createSurface(std::string& error);
  bool pickPhysicalDevice(std::string& error);
  bool createLogicalDevice(std::string& error);
  bool createSwapchain(std::string& error);
  bool createImageViews(std::string& error);
  bool createRenderPass(std::string& error);
  bool createDescriptorSetLayout(std::string& error);
  bool createGraphicsPipeline(std::string& error);
  bool createLinePipeline(std::string& error);
  bool createDepthResources(std::string& error);
  bool createFramebuffers(std::string& error);
  bool createCommandPool(std::string& error);
  bool createUniformBuffers(std::string& error);
  bool createDescriptorPool(std::string& error);
  bool createDescriptorSets(std::string& error);
  bool createCommandBuffers(std::string& error);
  bool createSyncObjects(std::string& error);
  bool initImGui(std::string& error);
  bool initImGuiVulkanBackend(std::string& error);

  void cleanupSwapchain();
  bool recreateSwapchain(std::string& error);

  bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                    Buffer& outBuffer, std::string& error);
  bool createImage(VkExtent2D extent, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                   VkMemoryPropertyFlags properties, Image& outImage, std::string& error);
  VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect,
                              std::string& error);
  bool copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size, std::string& error);
  void destroyBuffer(Buffer& buffer);
  void destroyImage(Image& image);
  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
  VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                               VkFormatFeatureFlags features) const;
  VkFormat findDepthFormat() const;
  bool hasStencilComponent(VkFormat format) const;

  bool uploadMeshBuffers(const StlMesh& mesh, std::string& error);
  bool uploadLineBuffers(const std::vector<ColorVertex>& lines, std::string& error);
  bool updateUniformBuffer(uint32_t frameIndex, const glm::mat4& view, const glm::mat4& projection,
                           std::string& error);
  bool recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, ImDrawData* drawData,
                           std::string& error);

  VkShaderModule createShaderModule(const std::vector<char>& code, std::string& error);
  std::vector<char> readBinaryFile(const std::string& path, std::string& error) const;

  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
  bool checkValidationLayerSupport() const;
  std::vector<const char*> getRequiredExtensions() const;
  static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
      VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
      VkDebugUtilsMessageTypeFlagsEXT messageTypes,
      const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData);

  GLFWwindow* window_ = nullptr;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;

  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  VkQueue presentQueue_ = VK_NULL_HANDLE;
  QueueFamilyIndices queueFamilies_;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
  VkExtent2D swapchainExtent_{};
  std::vector<VkImage> swapchainImages_;
  std::vector<VkImageView> swapchainImageViews_;

  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
  Image depthImage_;
  std::vector<VkFramebuffer> framebuffers_;

  VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline fillPipeline_ = VK_NULL_HANDLE;
  VkPipeline wireframePipeline_ = VK_NULL_HANDLE;
  VkPipeline linePipeline_ = VK_NULL_HANDLE;

  std::vector<Buffer> uniformBuffers_;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> descriptorSets_;

  Buffer vertexBuffer_;
  Buffer indexBuffer_;
  uint32_t indexCount_ = 0;

  Buffer lineVertexBuffer_;
  uint32_t lineVertexCount_ = 0;

  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> commandBuffers_;

  // This app uses double-buffered CPU frame pacing.
  // It is independent from swapchain image count.
  static constexpr size_t kFramesInFlight = 2;
  std::vector<FrameSync> syncObjects_;
  size_t currentFrame_ = 0;

  bool validationEnabled_ = false;
  bool supportsWireframe_ = false;
  bool wireframeEnabled_ = false;
  bool normalVisualizationEnabled_ = false;
  bool imguiInitialized_ = false;
  bool framebufferResized_ = false;
};
