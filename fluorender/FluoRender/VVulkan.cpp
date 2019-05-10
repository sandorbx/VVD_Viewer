#include "VVulkan.h"

void VVulkan::generateQuad()
{
	// Setup vertices for a single uv-mapped quad made from two triangles
	std::vector<Vertex> vertices =
	{
		{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f },{ 0.0f, 0.0f, 1.0f } },
		{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f },{ 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f },{ 0.0f, 0.0f, 1.0f } },
		{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f },{ 0.0f, 0.0f, 1.0f } }
	};

	// Setup indices
	std::vector<uint32_t> indices = { 0,1,2, 2,3,0 };
	indexCount = static_cast<uint32_t>(indices.size());

	// Create buffers
	// For the sake of simplicity we won't stage the vertex data to the gpu memory
	// Vertex buffer
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&vertexBuffer,
		vertices.size() * sizeof(Vertex),
		vertices.data()));
	// Index buffer
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&indexBuffer,
		indices.size() * sizeof(uint32_t),
		indices.data()));
}

void VVulkan::setupVertexDescriptions()
{
	// Binding description
	vertices.inputBinding.resize(1);
	vertices.inputBinding[0] =
		vks::initializers::vertexInputBindingDescription(
		0, 
		sizeof(Vertex), 
		VK_VERTEX_INPUT_RATE_VERTEX);

	// Attribute descriptions
	// Describes memory layout and shader positions
	vertices.inputAttributes.resize(3);
	// Location 0 : Position
	vertices.inputAttributes[0] =
		vks::initializers::vertexInputAttributeDescription(
		0,
		0,
		VK_FORMAT_R32G32B32_SFLOAT,
		offsetof(Vertex, pos));			
	// Location 1 : Texture coordinates
	vertices.inputAttributes[1] =
		vks::initializers::vertexInputAttributeDescription(
		0,
		1,
		VK_FORMAT_R32G32B32_SFLOAT,
		offsetof(Vertex, uv));

	vertices.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
	vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.inputBinding.size());
	vertices.inputState.pVertexBindingDescriptions = vertices.inputBinding.data();
	vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.inputAttributes.size());
	vertices.inputState.pVertexAttributeDescriptions = vertices.inputAttributes.data();
}

void VVulkan::setupDescriptorPool()
{
	// Example uses one ubo and one image sampler
	std::vector<VkDescriptorPoolSize> poolSizes =
	{
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
	};

	VkDescriptorPoolCreateInfo descriptorPoolInfo = 
		vks::initializers::descriptorPoolCreateInfo(
		static_cast<uint32_t>(poolSizes.size()),
		poolSizes.data(),
		2);

	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
}

void VVulkan::setupDescriptorSetLayout()
{
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = 
	{
		// Binding 0 : Vertex shader uniform buffer
		vks::initializers::descriptorSetLayoutBinding(
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 
		VK_SHADER_STAGE_VERTEX_BIT, 
		0),
		// Binding 1 : Fragment shader image sampler
		vks::initializers::descriptorSetLayoutBinding(
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
		VK_SHADER_STAGE_FRAGMENT_BIT, 
		1)
	};

	VkDescriptorSetLayoutCreateInfo descriptorLayout = 
		vks::initializers::descriptorSetLayoutCreateInfo(
		setLayoutBindings.data(),
		static_cast<uint32_t>(setLayoutBindings.size()));

	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

	VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
		vks::initializers::pipelineLayoutCreateInfo(
		&descriptorSetLayout,
		1);

	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
}

void VVulkan::setupDescriptorSet()
{
	VkDescriptorSetAllocateInfo allocInfo = 
		vks::initializers::descriptorSetAllocateInfo(
		descriptorPool,
		&descriptorSetLayout,
		1);

	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

	std::vector<VkWriteDescriptorSet> writeDescriptorSets =
	{
		// Binding 0 : Vertex shader uniform buffer
		vks::initializers::writeDescriptorSet(
		descriptorSet, 
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 
		0, 
		&uniformBufferVS.descriptor),
		// Binding 1 : Fragment shader texture sampler
		vks::initializers::writeDescriptorSet(
		descriptorSet, 
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
		1, 
		&texture.descriptor)
	};

	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
}

void VVulkan::preparePipelines()
{
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		0,
		VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
		VK_POLYGON_MODE_FILL,
		VK_CULL_MODE_NONE,
		VK_FRONT_FACE_COUNTER_CLOCKWISE,
		0);

	VkPipelineColorBlendAttachmentState blendAttachmentStateOVER =
		vks::initializers::pipelineColorBlendAttachmentState(
		VK_TRUE,
		VK_BLEND_OP_ADD,
		VK_BLEND_FACTOR_ONE,
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		0xf);
	VkPipelineColorBlendAttachmentState blendAttachmentStateADD =
		vks::initializers::pipelineColorBlendAttachmentState(
		VK_TRUE,
		VK_BLEND_OP_ADD,
		VK_BLEND_FACTOR_ONE,
		VK_BLEND_FACTOR_ONE,
		0xf);

	VkPipelineColorBlendStateCreateInfo colorBlendStateOVER =
		vks::initializers::pipelineColorBlendStateCreateInfo(
		1, 
		&blendAttachmentStateOVER);
	VkPipelineColorBlendStateCreateInfo colorBlendStateADD =
		vks::initializers::pipelineColorBlendStateCreateInfo(
		1, 
		&blendAttachmentStateADD);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(
		VK_FALSE,
		VK_FALSE,
		VK_COMPARE_OP_NEVER);

	VkPipelineViewportStateCreateInfo viewportState =
		vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

	VkPipelineMultisampleStateCreateInfo multisampleState =
		vks::initializers::pipelineMultisampleStateCreateInfo(
		VK_SAMPLE_COUNT_1_BIT,
		0);

	std::vector<VkDynamicState> dynamicStateEnables = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicState =
		vks::initializers::pipelineDynamicStateCreateInfo(
		dynamicStateEnables.data(),
		static_cast<uint32_t>(dynamicStateEnables.size()),
		0);

	// Load shaders
	std::array<VkPipelineShaderStageCreateInfo,2> shaderStages;

	shaderStages[0] = loadShader(getAssetPath() + "shaders/texture3d/texture3d.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	shaderStages[1] = loadShader(getAssetPath() + "shaders/texture3d/texture3d.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	VkGraphicsPipelineCreateInfo pipelineCreateInfo =
		vks::initializers::pipelineCreateInfo(
		pipelineLayout,
		renderPass,
		0);

	pipelineCreateInfo.pVertexInputState = &vertices.inputState;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendStateOVER;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.pDynamicState = &dynamicState;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCreateInfo.pStages = shaderStages.data();
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.over));

	pipelineCreateInfo.pColorBlendState = &colorBlendStateADD;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.add));
}

// Prepare and initialize uniform buffer containing shader uniforms
void VVulkan::prepareUniformBuffers()
{
	// Vertex shader uniform buffer block
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&uniformBufferVS,
		sizeof(uboVS),
		&uboVS));

	updateUniformBuffers();
}

void VVulkan::updateUniformBuffers(bool viewchanged)
{
	if (viewchanged)
	{
		uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
		glm::mat4 viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));

		uboVS.model = viewMatrix * glm::translate(glm::mat4(1.0f), cameraPos);
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uboVS.viewPos = glm::vec4(0.0f, 0.0f, -zoom, 0.0f);
	}
	else
	{
		uboVS.depth += frameTimer * 0.15f;
		if (uboVS.depth > 1.0f)
			uboVS.depth = uboVS.depth - 1.0f;
	}

	VK_CHECK_RESULT(uniformBufferVS.map());
	memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
	uniformBufferVS.unmap();
}

void VVulkan::prepare()
{
	VulkanExampleBase::prepare();
	generateQuad();
	setupVertexDescriptions();
	prepareUniformBuffers();
	setupDescriptorSetLayout();
	preparePipelines();
	setupDescriptorPool();
	setupDescriptorSet();
	prepared = true;
}

uint32_t VVulkan::findMemoryType(VkPhysicalDevice pdev, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(pdev, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	throw std::runtime_error("failed to find suitable memory type!");
}

void VVulkan::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create buffer!");
	}

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate buffer memory!");
	}

	vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void VVulkan::checkStagingBuffer(VkDeviceSize size)
{

}

VVulkan::VTexture VVulkan::GenTexture2D(VkFormat format, VkFilter filter, uint32_t w, uint32_t h)
{
	VVulkan::VTexture ret;

	ret.width = w;
	ret.height = h;
	ret.depth = 1;
	ret.mipLevels = 1;
	ret.format = format;
	ret.image = VK_NULL_HANDLE;
	ret.deviceMemory = VK_NULL_HANDLE;
	ret.sampler = VK_NULL_HANDLE;
	ret.view = VK_NULL_HANDLE;

	// Format support check
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(physicalDevice, ret.format, &formatProperties);
	// Check if format supports transfer
	if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
	{
		std::cout << "Error: Device does not support flag TRANSFER_DST for selected texture format!" << std::endl;
		return ret;
	}
	// Check if GPU supports requested 3D texture dimensions
	uint32_t maxImageDimension2D(vulkanDevice->properties.limits.maxImageDimension2D);
	if (width > maxImageDimension2D || height > maxImageDimension2D)
	{
		std::cout << "Error: Requested texture dimensions is greater than supported 2D texture dimension!" << std::endl;
		return ret;
	}

	// Create optimal tiled target image
	VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = ret.format;
	imageCreateInfo.mipLevels = ret.mipLevels;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.extent.width = ret.width;
	imageCreateInfo.extent.height = ret.height;
	imageCreateInfo.extent.depth = ret.depth;
	// Set initial layout of the image to undefined
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &ret.image));

	// Device local memory to back up image
	VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
	VkMemoryRequirements memReqs = {};
	vkGetImageMemoryRequirements(device, ret.image, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &ret.deviceMemory));
	VK_CHECK_RESULT(vkBindImageMemory(device, ret.image, ret.deviceMemory, 0));

	// Create sampler
	VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
	sampler.magFilter = filter;
	sampler.minFilter = filter;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.mipLodBias = 0.0f;
	sampler.compareOp = VK_COMPARE_OP_NEVER;
	sampler.minLod = 0.0f;
	sampler.maxLod = 0.0f;
	sampler.maxAnisotropy = 1.0;
	sampler.anisotropyEnable = VK_FALSE;
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &ret.sampler));

	// Create image view
	VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
	view.image = ret.image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = ret.format;
	view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.baseMipLevel = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount = 1;
	view.subresourceRange.levelCount = 1;
	VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &ret.view));

	// Fill image descriptor image info to be used descriptor set setup
	ret.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ret.descriptor.imageView = ret.view;
	ret.descriptor.sampler = ret.sampler;

	return ret;
}

VVulkan::VTexture VVulkan::GenTexture3D(VkFormat format, VkFilter filter, uint32_t w, uint32_t h, uint32_t d)
{
	VVulkan::VTexture ret;

	ret.width = w;
	ret.height = h;
	ret.depth = 1;
	ret.mipLevels = 1;
	ret.format = format;
	ret.image = VK_NULL_HANDLE;
	ret.deviceMemory = VK_NULL_HANDLE;
	ret.sampler = VK_NULL_HANDLE;
	ret.view = VK_NULL_HANDLE;

	// Format support check
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(physicalDevice, ret.format, &formatProperties);
	// Check if format supports transfer
	if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
	{
		std::cout << "Error: Device does not support flag TRANSFER_DST for selected texture format!" << std::endl;
		return ret;
	}
	// Check if GPU supports requested 3D texture dimensions
	uint32_t maxImageDimension3D(vulkanDevice->properties.limits.maxImageDimension3D);
	if (width > maxImageDimension3D || height > maxImageDimension3D)
	{
		std::cout << "Error: Requested texture dimensions is greater than supported 3D texture dimension!" << std::endl;
		return ret;
	}

	// Create optimal tiled target image
	VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
	imageCreateInfo.imageType = VK_IMAGE_TYPE_3D;
	imageCreateInfo.format = ret.format;
	imageCreateInfo.mipLevels = ret.mipLevels;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.extent.width = ret.width;
	imageCreateInfo.extent.height = ret.height;
	imageCreateInfo.extent.depth = ret.depth;
	// Set initial layout of the image to undefined
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &ret.image));

	// Device local memory to back up image
	VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
	VkMemoryRequirements memReqs = {};
	vkGetImageMemoryRequirements(device, ret.image, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &ret.deviceMemory));
	VK_CHECK_RESULT(vkBindImageMemory(device, ret.image, ret.deviceMemory, 0));

	// Create sampler
	VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
	sampler.magFilter = filter;
	sampler.minFilter = filter;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.mipLodBias = 0.0f;
	sampler.compareOp = VK_COMPARE_OP_NEVER;
	sampler.minLod = 0.0f;
	sampler.maxLod = 0.0f;
	sampler.maxAnisotropy = 1.0;
	sampler.anisotropyEnable = VK_FALSE;
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &ret.sampler));

	// Create image view
	VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
	view.image = ret.image;
	view.viewType = VK_IMAGE_VIEW_TYPE_3D;
	view.format = ret.format;
	view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.baseMipLevel = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount = 1;
	view.subresourceRange.levelCount = 1;
	VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &ret.view));

	// Fill image descriptor image info to be used descriptor set setup
	ret.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ret.descriptor.imageView = ret.view;
	ret.descriptor.sampler = ret.sampler;

	return ret;
}

bool VVulkan::UploadTexture3D(VVulkan::VTexture tex, void *data, VkOffset3D offset, VkExtent3D extent, uint32_t xpitch, uint32_t ypitch)
{

}