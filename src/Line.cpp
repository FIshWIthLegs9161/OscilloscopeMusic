#include "Line.h"
#include <fstream>

#define STAGING_BUFFER_SIZE (64 * 1024 * 1024)
#define VERTEX_BUFFER_SIZE (64 * 1024 * 1024)
#define INDEX_BUFFER_SIZE (64 * 1024 * 1024)

Vertex vertices[] = {
    { {  0.0f, -1.0f, 0.0f } },
    { {  1.0f,  1.0f, 0.0f } },
    { { -1.0f,  1.0f, 0.0f } }
};

uint32_t indices[] = {
    0, 1, 2
};

Line::Line(Renderer& renderer) {
    m_renderer = &renderer;
    m_device = &renderer.device();
    m_renderPass = &renderer.renderPass();

    createBuffers();
    createPipelineLayout();
    createPipeline();

    transferData(sizeof(vertices), &vertices, *m_vertexBuffer, vk::AccessFlags::VertexAttributeRead, vk::PipelineStageFlags::VertexInput);
    transferData(sizeof(indices), &indices, *m_indexBuffer, vk::AccessFlags::IndexRead, vk::PipelineStageFlags::VertexInput);
}

void Line::render(float dt, vk::CommandBuffer& commandBuffer) {
    handleTransfers(commandBuffer);

    vk::RenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.renderPass = &m_renderer->renderPass();
    renderPassInfo.framebuffer = &m_renderer->framebuffers()[m_renderer->index()];
    renderPassInfo.clearValues = { { } };
    renderPassInfo.renderArea = { {}, { m_renderer->width(), m_renderer->height() } };

    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::Inline);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::Graphics, *m_pipeline);

    vk::Viewport viewport = {};
    viewport.width = static_cast<float>(m_renderer->width());
    viewport.height = static_cast<float>(m_renderer->height());
    viewport.maxDepth = 1;

    vk::Rect2D scissor = {};
    scissor.extent.width = m_renderer->width();
    scissor.extent.height = m_renderer->height();

    commandBuffer.setViewport(0, viewport);
    commandBuffer.setScissor(0, scissor);

    vk::DeviceSize offset = 0;
    commandBuffer.bindVertexBuffers(0, { *m_vertexBuffer }, { offset });
    commandBuffer.bindIndexBuffer(*m_indexBuffer, 0, vk::IndexType::Uint32);

    commandBuffer.drawIndexed(3, 1, 0, 0, 0);

    commandBuffer.endRenderPass();
}

std::vector<char> Line::loadFile(const std::string& filename) {
    std::ifstream file(filename, std::fstream::ate | std::fstream::binary);
    size_t size = file.tellg();

    std::vector<char> data(size);

    file.seekg(0);
    file.read(data.data(), size);

    return data;
}

vk::ShaderModule Line::loadShader(const std::string& filename) {
    vk::ShaderModuleCreateInfo info = {};
    info.code = std::move(loadFile(filename));

    return vk::ShaderModule(*m_device, info);
}

void Line::transferData(size_t size, void* data, vk::Buffer& destinationBuffer, vk::AccessFlags destinationAccess, vk::PipelineStageFlags stage) {
    memcpy(&m_stagingPtr[m_stagingOffset], data, size);

    vk::BufferCopy copy = {};
    copy.size = static_cast<vk::DeviceSize>(size);
    copy.srcOffset = static_cast<vk::DeviceSize>(m_stagingOffset);

    vk::BufferMemoryBarrier barrier = {};
    barrier.buffer = &destinationBuffer;
    barrier.size = static_cast<vk::DeviceSize>(size);
    barrier.offset = static_cast<vk::DeviceSize>(m_stagingOffset);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask = vk::AccessFlags::HostWrite;
    barrier.dstAccessMask = destinationAccess;

    m_transfers.push_back({ &destinationBuffer, copy, barrier, stage });

    m_stagingOffset += size;
}

void Line::handleTransfers(vk::CommandBuffer& commandBuffer) {
    for (auto& transfer : m_transfers) {
        commandBuffer.copyBuffer(*m_stagingBuffer, *transfer.buffer, transfer.copy);

        commandBuffer.pipelineBarrier(vk::PipelineStageFlags::Host, transfer.stage, vk::DependencyFlags::None,
            nullptr, transfer.barrier, nullptr
        );
    }

    m_stagingOffset = 0;
    m_transfers.clear();
}

vk::DeviceMemory Line::allocateMemory(vk::Buffer& buffer, vk::MemoryPropertyFlags required, vk::MemoryPropertyFlags preferred) {
    vk::DeviceMemory memory = m_renderer->allocateMemory(buffer.requirements(), required, preferred);
    buffer.bind(memory, 0);
    return memory;
}

void Line::createBuffers() {
    {
        vk::BufferCreateInfo info = {};
        info.size = STAGING_BUFFER_SIZE;
        info.usage = vk::BufferUsageFlags::TransferSrc;

        m_stagingBuffer = std::make_unique<vk::Buffer>(m_renderer->device(), info);
        m_stagingBufferMemory = std::make_unique<vk::DeviceMemory>(allocateMemory(*m_stagingBuffer,
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent,
            vk::MemoryPropertyFlags::DeviceLocal));

        m_stagingPtr = static_cast<char*>(m_stagingBufferMemory->map(0, STAGING_BUFFER_SIZE));
    }

    {
        vk::BufferCreateInfo info = {};
        info.size = VERTEX_BUFFER_SIZE;
        info.usage = vk::BufferUsageFlags::TransferDst | vk::BufferUsageFlags::VertexBuffer;

        m_vertexBuffer = std::make_unique<vk::Buffer>(m_renderer->device(), info);
        m_vertexBufferMemory = std::make_unique<vk::DeviceMemory>(allocateMemory(*m_vertexBuffer,
            vk::MemoryPropertyFlags::DeviceLocal,
            vk::MemoryPropertyFlags::None));
    }

    {
        vk::BufferCreateInfo info = {};
        info.size = INDEX_BUFFER_SIZE;
        info.usage = vk::BufferUsageFlags::TransferDst | vk::BufferUsageFlags::IndexBuffer;

        m_indexBuffer = std::make_unique<vk::Buffer>(m_renderer->device(), info);
        m_indexBufferMemory = std::make_unique<vk::DeviceMemory>(allocateMemory(*m_indexBuffer,
        vk::MemoryPropertyFlags::DeviceLocal,
            vk::MemoryPropertyFlags::None));
    }

    {
        vk::BufferCreateInfo info = {};
        info.size = sizeof(UniformBuffer);
        info.usage = vk::BufferUsageFlags::TransferDst | vk::BufferUsageFlags::UniformBuffer;

        m_uniformBuffer = std::make_unique<vk::Buffer>(m_renderer->device(), info);
        m_uniformBufferMemory = std::make_unique<vk::DeviceMemory>(allocateMemory(*m_uniformBuffer,
            vk::MemoryPropertyFlags::HostVisible | vk::MemoryPropertyFlags::HostCoherent,
            vk::MemoryPropertyFlags::DeviceLocal));
    }
}

void Line::createPipelineLayout() {
    vk::PipelineLayoutCreateInfo info = {};

    m_pipelineLayout = std::make_unique<vk::PipelineLayout>(*m_device, info);
}

void Line::createPipeline() {
    vk::ShaderModule vertexShader = loadShader("shaders/line.vert.spv");
    vk::ShaderModule fragmentShader = loadShader("shaders/line.frag.spv");

    vk::PipelineShaderStageCreateInfo vertexStage = {};
    vertexStage.module = &vertexShader;
    vertexStage.name = "main";
    vertexStage.stage = vk::ShaderStageFlags::Vertex;

    vk::PipelineShaderStageCreateInfo fragmentStage = {};
    fragmentStage.module = &fragmentShader;
    fragmentStage.name = "main";
    fragmentStage.stage = vk::ShaderStageFlags::Fragment;

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.vertexAttributeDescriptions = {
        { 0, 0, vk::Format::R32G32B32_Sfloat, 0 },
        { 1, 0, vk::Format::R32G32B32_Sfloat, sizeof(glm::vec3) }
    };
    vertexInputInfo.vertexBindingDescriptions = {
        { 0, sizeof(Vertex), vk::VertexInputRate::Vertex }
    };

    vk::PipelineInputAssemblyStateCreateInfo inputInfo = {};
    inputInfo.topology = vk::PrimitiveTopology::TriangleList;

    vk::PipelineViewportStateCreateInfo viewportInfo = {};
    viewportInfo.viewports = { {} };
    viewportInfo.scissors = { {} };

    vk::PipelineRasterizationStateCreateInfo rasterizationInfo = {};
    rasterizationInfo.polygonMode = vk::PolygonMode::Fill;
    rasterizationInfo.cullMode = vk::CullModeFlags::Back;
    rasterizationInfo.frontFace = vk::FrontFace::Clockwise;
    rasterizationInfo.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisampleInfo = {};
    multisampleInfo.rasterizationSamples = vk::SampleCountFlags::_1;

    vk::PipelineColorBlendAttachmentState colorBlendAttachmentInfo = {};
    colorBlendAttachmentInfo.colorWriteMask = vk::ColorComponentFlags::R
                                  | vk::ColorComponentFlags::G
                                  | vk::ColorComponentFlags::B
                                  | vk::ColorComponentFlags::A;

    vk::PipelineColorBlendStateCreateInfo colorBlendInfo = {};
    colorBlendInfo.attachments = { colorBlendAttachmentInfo };

    vk::PipelineDynamicStateCreateInfo dynamicInfo = {};
    dynamicInfo.dynamicStates = {
        vk::DynamicState::Viewport,
        vk::DynamicState::Scissor
    };

    vk::GraphicsPipelineCreateInfo info = {};
    info.stages = {
        vertexStage,
        fragmentStage
    };
    info.vertexInputState = &vertexInputInfo;
    info.inputAssemblyState = &inputInfo;
    info.viewportState = &viewportInfo;
    info.rasterizationState = &rasterizationInfo;
    info.multisampleState = &multisampleInfo;
    info.colorBlendState = &colorBlendInfo;
    info.dynamicState = &dynamicInfo;
    info.layout = m_pipelineLayout.get();
    info.renderPass = m_renderPass;

    m_pipeline = std::make_unique<vk::GraphicsPipeline>(*m_device, info);
}