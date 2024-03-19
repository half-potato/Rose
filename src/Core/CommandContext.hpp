#include "Device.hpp"

namespace RoseEngine {

class CommandContext {
private:
	vk::raii::CommandBuffer mCommandBuffer = nullptr;
	ref<Device> mDevice = {};
	uint32_t mQueueFamily = {};
	uint64_t mSignalValue = 0;

	std::vector<vk::BufferMemoryBarrier2> mBufferBarrierQueue = {};
	std::vector<vk::ImageMemoryBarrier2>  mImageBarrierQueue = {};

public:
	inline       vk::raii::CommandBuffer& operator*()        { return mCommandBuffer; }
	inline const vk::raii::CommandBuffer& operator*() const  { return mCommandBuffer; }
	inline       vk::raii::CommandBuffer* operator->()       { return &mCommandBuffer; }
	inline const vk::raii::CommandBuffer* operator->() const { return &mCommandBuffer; }

	inline static ref<CommandContext> Create(const ref<Device> device, uint32_t queueFamily = -1) {
		if (queueFamily == -1) queueFamily = device->FindQueueFamily();

		ref<CommandContext> context = make_ref<CommandContext>();
		context->mDevice = device;
		context->mQueueFamily = queueFamily;

		auto commandBuffers = (*device)->allocateCommandBuffers(vk::CommandBufferAllocateInfo{
			.commandPool = device->GetCommandPool(queueFamily),
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1 });
		context->mCommandBuffer = std::move(commandBuffers[0]);

		context->mCommandBuffer.begin(vk::CommandBufferBeginInfo{});

		return context;
	}

	inline Device& GetDevice() const { return *mDevice; }

	inline uint64_t CurrentCounterValue() const { return GetDevice().TimelineSemaphore().getCounterValue(); }

	inline uint64_t Submit(const uint32_t queueIndex = 0, uint64_t waitValue = 0, vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eTopOfPipe) const {
		mCommandBuffer.end();

		const uint64_t signalValue = mDevice->IncrementTimelineCounter();

		vk::StructureChain<vk::SubmitInfo, vk::TimelineSemaphoreSubmitInfo> submitInfoChain = {};
		auto& submitInfo = submitInfoChain.get<vk::SubmitInfo>();
		auto& timelineSubmitInfo = submitInfoChain.get<vk::TimelineSemaphoreSubmitInfo>();

		submitInfo.setCommandBuffers(*mCommandBuffer);

		vk::Semaphore semaphore = *mDevice->TimelineSemaphore();

		submitInfo.setSignalSemaphores(semaphore);
		timelineSubmitInfo.setSignalSemaphoreValues(signalValue);

		if (waitValue != 0) {
			submitInfo.setWaitSemaphores(semaphore);
			submitInfo.setWaitDstStageMask(waitStage);
			timelineSubmitInfo.setWaitSemaphoreValues(waitValue);
		}

		(*mDevice)->getQueue(mQueueFamily, queueIndex).submit( submitInfo );

		return signalValue;
	}

	#pragma region Barriers

	inline static const vk::AccessFlags2 gWriteAccesses =
			vk::AccessFlagBits2::eShaderWrite |
			vk::AccessFlagBits2::eColorAttachmentWrite |
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
			vk::AccessFlagBits2::eTransferWrite |
			vk::AccessFlagBits2::eHostWrite |
			vk::AccessFlagBits2::eMemoryWrite |
			vk::AccessFlagBits2::eAccelerationStructureWriteKHR;

	inline void ExecuteBarriers() {
		vk::DependencyInfo info { .dependencyFlags = vk::DependencyFlagBits::eByRegion };
		info.setBufferMemoryBarriers(mBufferBarrierQueue);
		info.setImageMemoryBarriers(mImageBarrierQueue);
		mCommandBuffer.pipelineBarrier2(info);

		mBufferBarrierQueue.clear();
		mImageBarrierQueue.clear();
	}

	inline void AddBarrier(const vk::BufferMemoryBarrier2& barrier) { mBufferBarrierQueue.emplace_back(barrier); }
	inline void AddBarrier(const vk::ImageMemoryBarrier2& barrier)  { mImageBarrierQueue.emplace_back(barrier); }

	template<typename T>
	inline void AddBarrier(const BufferRange<T>& buffer, const Buffer::ResourceState& newState) {
		const auto& oldState = buffer.GetState();
		if (oldState.access == vk::AccessFlagBits2::eNone || newState.access == vk::AccessFlagBits2::eNone)
			return;
		if (!(oldState.access & gWriteAccesses) && !(newState.access & gWriteAccesses))
			return;
		AddBarrier(buffer.SetState(newState));

	}
	inline void AddBarrier(const ref<Image>& img, const vk::ImageSubresourceRange& subresource, const Image::ResourceState& newState) {
		auto barriers = img->SetSubresourceState(subresource, newState);
		for (const auto& barrier : barriers)
			AddBarrier(barrier);
	}
	inline void AddBarrier(const ImageView& img, const Image::ResourceState& newState) {
		auto barriers = img.SetState(newState);
		for (const auto& barrier : barriers)
			AddBarrier(barrier);
	}

	#pragma endregion

	#pragma region Copy, fill, etc.

	template<typename T> requires(sizeof(T) == sizeof(uint32_t))
	inline void Fill(const BufferRange<T>& buffer, const T data, const vk::DeviceSize offset = 0, const vk::DeviceSize size = VK_WHOLE_SIZE) {
		AddBarrier(buffer, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eTransfer,
			.access = vk::AccessFlagBits2::eTransferWrite });
		ExecuteBarriers();
		mCommandBuffer.fillBuffer(**buffer.mBuffer, offset, std::min(size, buffer.size_bytes()), std::bit_cast<uint32_t>(data));
	}

	template<typename Tx, typename Ty>
	inline void Copy(const BufferRange<Tx>& src, const BufferRange<Ty>& dst) {
		if (dst.size_bytes() < src.size_bytes())
			throw std::runtime_error("dst smaller than src: " + std::to_string(dst.size_bytes()) + " < " + std::to_string(src.size_bytes()));
		AddBarrier(src, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eTransfer,
			.access = vk::AccessFlagBits2::eTransferRead });
		AddBarrier(dst, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eTransfer,
			.access = vk::AccessFlagBits2::eTransferWrite });

		ExecuteBarriers();

		mCommandBuffer.copyBuffer(
			**src.mBuffer,
			**dst.mBuffer,
			vk::BufferCopy{
				.srcOffset = src.mOffset,
				.dstOffset = dst.mOffset,
				.size = src.size_bytes() });
	}
	template<typename T>
	inline void Copy(const BufferRange<T>& src, const ImageView& dst, const uint32_t dstLevel = 0) {
		AddBarrier(src, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eTransfer,
			.access = vk::AccessFlagBits2::eTransferRead });
		AddBarrier(dst,
			Image::ResourceState{
				.layout = vk::ImageLayout::eTransferDstOptimal,
				.stage = vk::PipelineStageFlagBits2::eTransfer,
				.access = vk::AccessFlagBits2::eTransferWrite });

		ExecuteBarriers();

		mCommandBuffer.copyBufferToImage(
			**src.mBuffer,
			**dst.mImage,
			vk::ImageLayout::eTransferDstOptimal,
			vk::BufferImageCopy{
				.bufferOffset = src.mOffset,
				.bufferRowLength = 0,
				.bufferImageHeight = 0,
				.imageSubresource = dst.GetSubresourceLayer(dstLevel),
				.imageOffset = { 0, 0, 0 },
				.imageExtent = vk::Extent3D{dst.Extent().x, dst.Extent().y, dst.Extent().z} });
	}

	inline void Copy(const ref<Image>& src, const ref<Image>& dst, const vk::ArrayProxy<const vk::ImageCopy>& regions) {
		for (const vk::ImageCopy& region : regions) {
			const auto& s = region.srcSubresource;
			AddBarrier(src,
				vk::ImageSubresourceRange{
					.aspectMask     = region.srcSubresource.aspectMask,
					.baseMipLevel   = region.srcSubresource.mipLevel,
					.levelCount     = 1,
					.baseArrayLayer = region.srcSubresource.baseArrayLayer,
					.layerCount     = region.srcSubresource.layerCount },
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferSrcOptimal,
					.stage = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferRead });
			AddBarrier(dst,
				vk::ImageSubresourceRange{
					.aspectMask     = region.dstSubresource.aspectMask,
					.baseMipLevel   = region.dstSubresource.mipLevel,
					.levelCount     = 1,
					.baseArrayLayer = region.dstSubresource.baseArrayLayer,
					.layerCount     = region.dstSubresource.layerCount },
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferDstOptimal,
					.stage = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferWrite });
		}

		ExecuteBarriers();

		mCommandBuffer.copyImage(
			**src,
			vk::ImageLayout::eTransferSrcOptimal,
			**dst,
			vk::ImageLayout::eTransferDstOptimal,
			regions);
	}
	inline void Copy(const ImageView& src, const ImageView& dst, uint32_t srcMip = 0, uint32_t dstMip = 0) {
		Copy(src.mImage, dst.mImage, vk::ImageCopy{
			.srcSubresource = src.GetSubresourceLayer(srcMip),
			.srcOffset = vk::Offset3D{ 0, 0, 0 },
			.dstSubresource = dst.GetSubresourceLayer(dstMip),
			.dstOffset = vk::Offset3D{ 0, 0, 0 },
			.extent = vk::Extent3D{dst.Extent().x, dst.Extent().y, dst.Extent().z} });
	}

	inline void Blit(const ref<Image>& src, const ref<Image>& dst, const vk::ArrayProxy<const vk::ImageBlit>& regions, const vk::Filter filter) {
		for (const vk::ImageBlit& region : regions) {
			AddBarrier(src,
				vk::ImageSubresourceRange{
					.aspectMask     = region.srcSubresource.aspectMask,
					.baseMipLevel   = region.srcSubresource.mipLevel,
					.levelCount     = 1,
					.baseArrayLayer = region.srcSubresource.baseArrayLayer,
					.layerCount     = region.srcSubresource.layerCount },
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferSrcOptimal,
					.stage = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferRead });
			AddBarrier(dst,
				vk::ImageSubresourceRange{
					.aspectMask     = region.dstSubresource.aspectMask,
					.baseMipLevel   = region.dstSubresource.mipLevel,
					.levelCount     = 1,
					.baseArrayLayer = region.dstSubresource.baseArrayLayer,
					.layerCount     = region.dstSubresource.layerCount },
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferDstOptimal,
					.stage = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferWrite });
		}

		ExecuteBarriers();

		mCommandBuffer.blitImage(**src, vk::ImageLayout::eTransferSrcOptimal, **dst, vk::ImageLayout::eTransferDstOptimal, regions, filter);
	}
	inline void Blit(const ImageView& src, const ImageView& dst, const vk::Filter filter = vk::Filter::eLinear) {
		Blit(
			src.mImage,
			dst.mImage,
			vk::ImageBlit{
				.srcSubresource = src.GetSubresourceLayer(),
				.srcOffsets = std::array<vk::Offset3D, 2>{
					vk::Offset3D{ 0, 0, 0 },
					std::bit_cast<vk::Offset3D>(src.Extent()) },
				.dstSubresource = dst.GetSubresourceLayer(),
				.dstOffsets = std::array<vk::Offset3D, 2>{
					vk::Offset3D{ 0, 0, 0 },
					std::bit_cast<vk::Offset3D>(dst.Extent()) } },
			filter);
	}

	inline void GenerateMipMaps(const ref<Image>& img, const vk::Filter filter = vk::Filter::eLinear, const vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor) {
		vk::ImageBlit blit = {
			.srcSubresource = vk::ImageSubresourceLayers{
				.aspectMask = aspect,
				.baseArrayLayer = 0,
				.layerCount = img->Info().arrayLayers },
			.srcOffsets = std::array<vk::Offset3D, 2>{
				vk::Offset3D{ 0, 0, 0 },
				std::bit_cast<vk::Offset3D>(img->Info().extent) },
			.dstSubresource = blit.srcSubresource,
			.dstOffsets = std::array<vk::Offset3D, 2>{
				vk::Offset3D{ 0, 0, 0 },
				vk::Offset3D{ 0, 0, 0 } } };

		for (uint32_t i = 1; i < img->Info().mipLevels; i++) {
			AddBarrier(img,
				vk::ImageSubresourceRange{
					.aspectMask = aspect,
					.baseMipLevel = i - 1,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = img->Info().arrayLayers },
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferSrcOptimal,
					.stage  = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferRead });
			AddBarrier(img,
				vk::ImageSubresourceRange{
					.aspectMask = aspect,
					.baseMipLevel = i,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = img->Info().arrayLayers },
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferDstOptimal,
					.stage  = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferWrite });
			ExecuteBarriers();

			blit.srcSubresource.mipLevel = i - 1;
			blit.dstSubresource.mipLevel = i;
			blit.dstOffsets[1].x = std::max(1, blit.srcOffsets[1].x / 2);
			blit.dstOffsets[1].y = std::max(1, blit.srcOffsets[1].y / 2);
			blit.dstOffsets[1].z = std::max(1, blit.srcOffsets[1].z / 2);

			mCommandBuffer.blitImage(
				**img, vk::ImageLayout::eTransferSrcOptimal,
				**img, vk::ImageLayout::eTransferDstOptimal,
				blit, vk::Filter::eLinear);

			blit.srcOffsets[1] = blit.dstOffsets[1];
		}
	}

	inline void ClearColor(const ref<Image>& img, const vk::ClearColorValue& clearValue, const vk::ArrayProxy<const vk::ImageSubresourceRange>& subresources) {
		for (const auto& subresource : subresources)
			AddBarrier(img,
				subresource,
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferDstOptimal,
					.stage  = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferWrite });

		ExecuteBarriers();

		mCommandBuffer.clearColorImage(**img, vk::ImageLayout::eTransferDstOptimal, clearValue, subresources);
	}
	inline void ClearColor(const ImageView& img, const vk::ClearColorValue& clearValue) {
		ClearColor(img.mImage, clearValue, img.mSubresource);
	}

	#pragma endregion
};

}