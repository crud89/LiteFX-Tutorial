#include "main.h"

struct Vertex {
	Vector4f position;
	Vector4f color;
};

class SampleApp : public LiteFX::App {
public:
	static StringView Name() noexcept { return "My LiteFX App"sv; }
	StringView name() const noexcept override { return Name(); }

	static AppVersion Version() { return AppVersion(1, 0, 0, 0); }
	AppVersion version() const noexcept override { return Version(); }

private:
	GlfwWindowPtr m_window;
	Optional<UInt32> m_adapterId;
	SharedPtr<Viewport> m_viewport;
	SharedPtr<Scissor> m_scissor;
	IGraphicsDevice* m_device;

	std::array<Vertex, 3> m_vertices {
		Vertex { { 0.1, 0.1, 1.0, 1.0 }, { 1.0, 0.0, 0.0, 1.0 } },
		Vertex { { 0.9, 0.1, 1.0, 1.0 }, { 0.0, 1.0, 0.0, 1.0 } },
		Vertex { { 0.5, 0.9, 1.0, 1.0 }, { 0.0, 0.0, 1.0, 1.0 } }
	};
	std::array<UInt16, 3> m_indices { 0, 1, 2 };

public:
	SampleApp(GlfwWindowPtr&& window, Optional<UInt32> adapterId) :
		App(), m_window(std::move(window)), m_adapterId(adapterId)
	{
		this->initializing += std::bind(&SampleApp::onInit, this);
		this->startup += std::bind(&SampleApp::onStartup, this);
		this->resized += std::bind(&SampleApp::onResize, this, std::placeholders::_1, std::placeholders::_2);
		this->shutdown += std::bind(&SampleApp::onShutdown, this);
	}

private:
	void onInit();
	void onStartup();
	void onShutdown();
	void onResize(const void* sender, const ResizeEventArgs& e);
};

void SampleApp::onStartup()
{
	// Request the input assembler from the pipeline state.
	auto& geometryPipeline = dynamic_cast<IRenderPipeline&>(m_device->state().pipeline("Geometry Pipeline"));
	auto inputAssembler = geometryPipeline.inputAssembler();

	// Create a new command buffer from the transfer queue.
	auto& transferQueue = m_device->defaultQueue(QueueType::Transfer);
	auto commandBuffer = transferQueue.createCommandBuffer(true);

	// Create the vertex buffer and transfer the staging buffer into it.
	auto vertexBuffer = m_device->factory().createVertexBuffer("Vertex Buffer", inputAssembler->vertexBufferLayout(0), ResourceHeap::Resource, static_cast<UInt32>(m_vertices.size()));
	commandBuffer->transfer(m_vertices.data(), m_vertices.size() * sizeof(::Vertex), *vertexBuffer, 0, static_cast<UInt32>(m_vertices.size()));

	// Create the index buffer and transfer the staging buffer into it.
	auto indexBuffer = m_device->factory().createIndexBuffer("Index Buffer", *inputAssembler->indexBufferLayout(), ResourceHeap::Resource, static_cast<UInt32>(m_indices.size()));
	commandBuffer->transfer(m_indices.data(), m_indices.size() * inputAssembler->indexBufferLayout()->elementSize(), *indexBuffer, 0, static_cast<UInt32>(m_indices.size()));

	// Submit the command buffer and wait for its execution.
	auto fence = commandBuffer->submit();
	transferQueue.waitFor(fence);

    // This is the main application loop. Add any per-frame logic below.
    while (!::glfwWindowShouldClose(m_window.get()))
    {
		// Poll UI events.
		::glfwPollEvents();

		// Swap the back buffers for the next frame.
		auto backBuffer = m_device->swapChain().swapBackBuffer();

		// Query state. For performance reasons, those state variables should be cached for more complex applications, instead of looking them up every frame.
		auto& frameBuffer = m_device->state().frameBuffer(std::format("Frame Buffer {0}", backBuffer));
		auto& renderPass = m_device->state().renderPass("Geometry");

		// Begin rendering on the render pass and use the only pipeline we've created for it.
		renderPass.begin(frameBuffer);
		auto commandBuffer = renderPass.commandBuffer(0);
		commandBuffer->use(geometryPipeline);
		commandBuffer->setViewports(m_viewport.get());
		commandBuffer->setScissors(m_scissor.get());

		// Bind the vertex and index buffers.
		commandBuffer->bind(*vertexBuffer);
		commandBuffer->bind(*indexBuffer);

		// Draw the object and present the frame by ending the render pass.
		commandBuffer->drawIndexed(indexBuffer->elements());
		renderPass.end();
    }
}

void SampleApp::onShutdown()
{
    ::glfwDestroyWindow(m_window.get());
    ::glfwTerminate();
}

void SampleApp::onInit()
{
    ::glfwSetWindowUserPointer(m_window.get(), this);

    // The following callback can be used to handle resize-events and is an example how to forward events from glfw to the engine.
    ::glfwSetFramebufferSizeCallback(m_window.get(), [](GLFWwindow* window, int width, int height) {
        auto app = static_cast<SampleApp*>(::glfwGetWindowUserPointer(window));
        app->resize(width, height);
    });

    // This callback is used when a graphics backend is started.
    auto startCallback = [this]<typename TBackend>(TBackend* backend) {
		// Alias type names for improved readability.
		using RenderPass = TBackend::render_pass_type;
		using FrameBuffer = TBackend::frame_buffer_type;
		using ShaderProgram = TBackend::shader_program_type;
		using InputAssembler = TBackend::input_assembler_type;
		using Rasterizer = TBackend::rasterizer_type;
		using RenderPipeline = TBackend::render_pipeline_type;

        // Store the window handle.
        auto window = m_window.get();

        // Get the proper frame buffer size.
        int width{}, height{};
        ::glfwGetFramebufferSize(window, &width, &height);

		// Create viewport and scissors.
		m_viewport = makeShared<Viewport>(RectF(0.f, 0.f, static_cast<Float>(width), static_cast<Float>(height)));
		m_scissor = makeShared<Scissor>(RectF(0.f, 0.f, static_cast<Float>(width), static_cast<Float>(height)));

		// Find the adapter, create a surface and initialize the device.
		auto adapter = backend->findAdapter(m_adapterId);

		if (adapter == nullptr)
			adapter = backend->findAdapter(std::nullopt);

		auto surface = backend->createSurface(::glfwGetWin32Window(window));

		// Create the device.
		auto device = std::addressof(backend->createDevice("Default", *adapter, std::move(surface), Format::B8G8R8A8_UNORM, m_viewport->getRectangle().extent(), 3, false));

		// Create a render pass.
		SharedPtr<RenderPass> renderPass = device->buildRenderPass("Geometry")
			.renderTarget("Color Target", RenderTargetType::Present, Format::B8G8R8A8_UNORM, RenderTargetFlags::Clear, { 0.1f, 0.1f, 0.1f, 1.f });

		// Create the frame buffer.
		auto frameBuffers = std::views::iota(0u, device->swapChain().buffers()) |
			std::views::transform([&](UInt32 index) { return device->makeFrameBuffer(std::format("Frame Buffer {0}", index), device->swapChain().renderArea()); }) |
			std::ranges::to<Array<SharedPtr<FrameBuffer>>>();

		std::ranges::for_each(frameBuffers, [&renderPass](auto& frameBuffer) { frameBuffer->addImages(renderPass->renderTargets()); });

		// Find the proper file extension.
		String extension;

		if constexpr (std::is_same_v<TBackend, VulkanBackend>)
			extension = "spv";
		else if constexpr (std::is_same_v<TBackend, DirectX12Backend>)
			extension = "dxi";

		// Create the shader program.
		SharedPtr<ShaderProgram> shaderProgram = device->buildShaderProgram()
			.withVertexShaderModule(std::format("shaders/tutorial_vs.{}", extension))
			.withFragmentShaderModule(std::format("shaders/tutorial_fs.{}", extension));

		// Create input assembler state.
		SharedPtr<InputAssembler> inputAssembler = device->buildInputAssembler()
			.topology(PrimitiveTopology::TriangleList)
			.indexType(IndexType::UInt16)
			.vertexBuffer(sizeof(Vertex), 0)
				.withAttribute(0, BufferFormat::XYZW32F, offsetof(Vertex, position), AttributeSemantic::Position)
				.withAttribute(1, BufferFormat::XYZW32F, offsetof(Vertex, color), AttributeSemantic::Color)
				.add();

		// Create rasterizer state.
		SharedPtr<Rasterizer> rasterizer = device->buildRasterizer()
			.polygonMode(PolygonMode::Solid)
			.cullMode(CullMode::BackFaces)
			.cullOrder(CullOrder::CounterClockWise);

		// Create a render pipeline.
		UniquePtr<RenderPipeline> renderPipeline = device->buildRenderPipeline(*renderPass, "Geometry Pipeline")
			.inputAssembler(inputAssembler)
			.rasterizer(rasterizer)
			.shaderProgram(shaderProgram)
			.layout(shaderProgram->reflectPipelineLayout());

		// Store state resources in the device state.
		device->state().add(std::move(renderPass));
		device->state().add(std::move(renderPipeline));
		std::ranges::for_each(frameBuffers, [device](auto& frameBuffer) { device->state().add(std::move(frameBuffer)); });

		// Store the device and return,
		m_device = device;
		return true;
    };

    // This callback is the opposite and is called to stop a backend.
    auto stopCallback = []<typename TBackend>(TBackend* backend) {
		backend->releaseDevice("Default");
    };

#ifdef LITEFX_BUILD_VULKAN_BACKEND
    // Register the Vulkan backend de-/initializer.
    this->onBackendStart<VulkanBackend>(startCallback);
    this->onBackendStop<VulkanBackend>(stopCallback);
#endif // LITEFX_BUILD_VULKAN_BACKEND

#ifdef LITEFX_BUILD_DIRECTX_12_BACKEND
    // Register the DirectX 12 backend de-/initializer.
    this->onBackendStart<DirectX12Backend>(startCallback);
    this->onBackendStop<DirectX12Backend>(stopCallback);
#endif // LITEFX_BUILD_DIRECTX_12_BACKEND
}

void SampleApp::onResize(const void* /*sender*/, const ResizeEventArgs& e)
{
	// TODO: Handle resize event.
}

int main(const int argc, const char** argv)
{
#ifdef WIN32
	// Enable console colors.
	HANDLE console = ::GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD consoleMode = 0;

	if (console == INVALID_HANDLE_VALUE || !::GetConsoleMode(console, &consoleMode))
		return static_cast<int>(::GetLastError());

	::SetConsoleMode(console, consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif

	// Store the app name.
	const String appName{ SampleApp::Name() };

	// Create glfw window.
	if (!::glfwInit())
		throw std::runtime_error("Unable to initialize glfw.");

	::glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	::glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	auto window = GlfwWindowPtr(::glfwCreateWindow(800, 600, appName.c_str(), nullptr, nullptr));

#ifdef LITEFX_BUILD_VULKAN_BACKEND
	// Get the required Vulkan extensions from glfw.
	uint32_t extensions = 0;
	const char** extensionNames = ::glfwGetRequiredInstanceExtensions(&extensions);
	Array<String> requiredExtensions;

	for (uint32_t i(0); i < extensions; ++i)
		requiredExtensions.emplace_back(extensionNames[i]);
#endif // LITEFX_BUILD_VULKAN_BACKEND

	// Create the app.
	try
	{
		UniquePtr<App> app = App::build<SampleApp>(std::move(window), std::nullopt)
			.logTo<ConsoleSink>(LogLevel::Trace)
			.logTo<RollingFileSink>("sample.log", LogLevel::Debug)
#ifdef LITEFX_BUILD_VULKAN_BACKEND
			.useBackend<VulkanBackend>(requiredExtensions)
#endif // LITEFX_BUILD_VULKAN_BACKEND
#ifdef LITEFX_BUILD_DIRECTX_12_BACKEND
			.useBackend<DirectX12Backend>()
#endif // LITEFX_BUILD_DIRECTX_12_BACKEND
			;

		app->run();
	}
	catch (const LiteFX::Exception& ex)
	{
		std::cerr << "\033[3;41;37mUnhandled exception: " << ex.what() << '\n' << "at: " << ex.trace() << "\033[0m\n";

		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}