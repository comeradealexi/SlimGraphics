#pragma once
#include "sgPlatformForwardDeclare.h"
#include <sgTypes.h>
#include "sgVulkanDevice.h"
#include "sgVulkanShader.h"
#include "sgVulkanMemory.h"
#include "sgVulkanBinding.h"
#include "sgVulkanCommandList.h"
#include "sgVulkanCommandQueue.h"
#include "sgVulkanBuffer.h"
#include "sgVulkanTexture.h"
#include "sgVulkanShaderResourceView.h"
#include "sgVulkanBufferView.h"
#include "sgVulkanPipeline.h"
#include "sgVulkanRenderTargetView.h"
#include "sgVulkanConstantBufferView.h"
#include "sgVulkanVertexBufferView.h"
#include "sgVulkanIndexBufferView.h"
#include "sgVulkanUnorderedAccessView.h"
#include "sgVulkanGPUTimestampPool.h"
#include "sgVulkanGPUStatisticPool.h"

#ifndef SG_PLATFORM_VULKAN
#error Expecting SG_PLATFORM_VULKAN to be defined
#endif