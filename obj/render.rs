use std::sync::Arc;

use winit::dpi::PhysicalSize;
use winit::window::Window;

use vulkano::buffer::cpu_pool::CpuBufferPoolChunk;
use vulkano::buffer::{BufferUsage, CpuBufferPool, ImmutableBuffer};
use vulkano::command_buffer::{
    AutoCommandBufferBuilder, DynamicState, SubpassContents,
};
use vulkano::descriptor::descriptor_set::{
    PersistentDescriptorSet, PersistentDescriptorSetImg,
    PersistentDescriptorSetSampler,
};
use vulkano::descriptor::PipelineLayoutAbstract;
use vulkano::device::{Device, DeviceExtensions, Queue, QueuesIter};
use vulkano::format::Format;
use vulkano::framebuffer::{
    Framebuffer, FramebufferAbstract, RenderPassAbstract, Subpass,
};
use vulkano::image::attachment::AttachmentImage;
use vulkano::image::immutable::ImmutableImage;
use vulkano::image::{Dimensions, ImageUsage, MipmapsCount, SwapchainImage};
use vulkano::instance::{Instance, PhysicalDevice};
use vulkano::memory::pool::StdMemoryPool;
use vulkano::pipeline::viewport::Viewport;
use vulkano::pipeline::{GraphicsPipeline, GraphicsPipelineAbstract};
use vulkano::sampler::{Filter, MipmapMode, Sampler, SamplerAddressMode};
use vulkano::swapchain::{
    AcquireError, ColorSpace, FullscreenExclusive, PresentMode, Surface,
    SurfaceTransform, Swapchain,
};
use vulkano::sync::{FlushError, GpuFuture};

use cgmath::{Matrix4, Rad};

use crate::types::{Font, Mvp, Obj, Vertex};

pub struct Renderer {
    surface: Arc<Surface<Window>>,
    logical: Arc<Device>,
    queue: Arc<Queue>,

    swapchain: Arc<Swapchain<Window>>,
    render_pass: Arc<dyn RenderPassAbstract + Send + Sync>,
    pipeline: Arc<dyn GraphicsPipelineAbstract + Send + Sync>,
    framebuffers: Vec<Arc<dyn FramebufferAbstract + Send + Sync>>,
    dimensions: [f32; 2],

    vertex_shader: obj_vs::Shader,
    frag_shader: obj_fs::Shader,
    sampler: Arc<Sampler>,

    uniform_buffer: CpuBufferPool<Mvp>,
    vertex_buffer: Arc<ImmutableBuffer<[Vertex]>>,
    index_buffer: Arc<ImmutableBuffer<[u32]>>,
    texture_buffer: Arc<ImmutableImage<Format>>,

    overlay: Option<TextOverlay>,

    swapchain_outdated: bool,
    previous_frame_end: Option<Box<dyn GpuFuture>>,
}

impl Renderer {
    pub fn new(window: Window, obj: Obj) -> Self {
        let instance =
            Instance::new(None, &vulkano_win::required_extensions(), None)
                .unwrap();

        let physical = PhysicalDevice::enumerate(&instance).next().unwrap();
        println!("physical: {}, {:?}", physical.name(), physical.ty());

        let surface =
            vulkano_win::create_vk_surface(window, instance.clone()).unwrap();

        surface.window().set_cursor_visible(true);

        let (logical, mut queues) =
            Renderer::create_logical(physical, &surface);

        let queue = queues.next().unwrap();

        let (swapchain, images) = Renderer::create_swapchain(
            physical.clone(),
            &surface,
            logical.clone(),
            &queue,
        );

        let render_pass =
            Renderer::create_render_pass(logical.clone(), swapchain.format());

        let vertex_shader = obj_vs::Shader::load(logical.clone()).unwrap();
        let frag_shader = obj_fs::Shader::load(logical.clone()).unwrap();

        let dimensions = [
            images[0].dimensions()[0] as f32,
            images[0].dimensions()[1] as f32,
        ];
        let pipeline = Renderer::create_pipeline(
            logical.clone(),
            &vertex_shader,
            &frag_shader,
            dimensions,
            render_pass.clone(),
        );

        let framebuffers = Renderer::create_framebuffers(
            logical.clone(),
            &images,
            render_pass.clone(),
        );

        let uniform_buffer = CpuBufferPool::<Mvp>::new(
            logical.clone(),
            BufferUsage::uniform_buffer(),
        );

        let (vertex_buffer, vbuf_future) = ImmutableBuffer::from_iter(
            obj.vertices.iter().cloned(),
            BufferUsage::vertex_buffer(),
            queue.clone(),
        )
        .unwrap();
        vbuf_future.flush().unwrap();

        let (index_buffer, ibuf_future) = ImmutableBuffer::from_iter(
            obj.indices.iter().cloned(),
            BufferUsage::index_buffer(),
            queue.clone(),
        )
        .unwrap();
        ibuf_future.flush().unwrap();

        let (texture_buffer, tex_future) = if let Some(texture) = obj.texture {
            let buf = texture.into_bgra8();
            let (width, height) = (buf.width(), buf.height());
            ImmutableImage::from_iter(
                buf.into_raw().iter().cloned(),
                Dimensions::Dim2d { width, height },
                MipmapsCount::One,
                swapchain.format(),
                queue.clone(),
            )
            .unwrap()
        } else {
            let img: Vec<u8> = Vec::from([255, 255, 255, 255]);
            ImmutableImage::from_iter(
                img.into_iter(),
                Dimensions::Dim2d {
                    width: 1,
                    height: 1,
                },
                MipmapsCount::One,
                swapchain.format(),
                queue.clone(),
            )
            .unwrap()
        };
        tex_future.flush().unwrap();

        let sampler = Sampler::simple_repeat_linear_no_mipmap(logical.clone());
        let previous_frame_end =
            Some(vulkano::sync::now(logical.clone()).boxed());

        Renderer {
            surface,
            logical,
            queue,
            swapchain,
            render_pass,
            pipeline,
            framebuffers,
            dimensions,
            vertex_shader,
            frag_shader,
            sampler,
            uniform_buffer,
            vertex_buffer,
            index_buffer,
            texture_buffer,
            overlay: None,
            swapchain_outdated: false,
            previous_frame_end,
        }
    }

    pub fn with_overlay(mut self, font: Font) -> Self {
        self.overlay = Some(TextOverlay::new(
            self.logical.clone(),
            self.queue.clone(),
            self.swapchain.format(),
            self.dimensions,
            font,
        ));
        self
    }

    pub fn swapchain_outdated(&mut self) {
        self.swapchain_outdated = true;
    }

    pub fn redraw(&mut self, model: Matrix4<f32>, view: Matrix4<f32>) {
        self.previous_frame_end.as_mut().unwrap().cleanup_finished();

        if self.swapchain_outdated {
            self.recreate_swapchain();
        }

        let PhysicalSize { width, height } =
            self.surface.window().inner_size();
        let aspect = width as f32 / height as f32;
        let mvp = Mvp {
            model: model,
            view: view,
            proj: cgmath::perspective(Rad(1.0), aspect, 0.1, 10000.0),
        };

        let uniform_subbuffer = self.uniform_buffer.next(mvp).unwrap();

        let layout = self.pipeline.descriptor_set_layout(0).unwrap();
        let set = Arc::new(
            PersistentDescriptorSet::start(layout.clone())
                .add_buffer(uniform_subbuffer)
                .unwrap()
                .add_sampled_image(
                    self.texture_buffer.clone(),
                    self.sampler.clone(),
                )
                .unwrap()
                .build()
                .unwrap(),
        );

        let (image_num, suboptimal, acquire_future) =
            match vulkano::swapchain::acquire_next_image(
                self.swapchain.clone(),
                None,
            ) {
                Ok(r) => r,
                Err(AcquireError::OutOfDate) => {
                    self.swapchain_outdated = true;
                    return;
                }
                Err(e) => panic!("Failed to acquire next image: {:?}", e),
            };
        if suboptimal {
            self.swapchain_outdated = true;
        }

        let mut builder = AutoCommandBufferBuilder::primary_one_time_submit(
            self.logical.clone(),
            self.queue.family(),
        )
        .unwrap();
        let clear_values = vec![[0.0, 0.0, 0.0, 0.0].into(), 1f32.into()];
        builder
            .begin_render_pass(
                self.framebuffers[image_num].clone(),
                SubpassContents::Inline,
                clear_values,
            )
            .unwrap()
            .draw_indexed(
                self.pipeline.clone(),
                &DynamicState::none(),
                vec![self.vertex_buffer.clone()],
                self.index_buffer.clone(),
                set.clone(),
                (),
                vec![],
            )
            .unwrap();

        if let Some(overlay) = &self.overlay {
            builder
                .draw_indexed(
                    overlay.pipeline.clone(),
                    &DynamicState::none(),
                    vec![overlay.vertex_chunk.clone()],
                    overlay.index_chunk.clone(),
                    overlay.set.clone(),
                    (),
                    vec![],
                )
                .unwrap();
        }
        builder.end_render_pass().unwrap();
        let command_buffer = builder.build().unwrap();

        let future = self
            .previous_frame_end
            .take()
            .unwrap()
            .join(acquire_future)
            .then_execute(self.queue.clone(), command_buffer)
            .unwrap()
            .then_swapchain_present(
                self.queue.clone(),
                self.swapchain.clone(),
                image_num,
            )
            .then_signal_fence_and_flush();

        match future {
            Ok(future) => {
                self.previous_frame_end = Some(future.boxed());
            }
            Err(FlushError::OutOfDate) => {
                self.swapchain_outdated = true;
                self.previous_frame_end =
                    Some(vulkano::sync::now(self.logical.clone()).boxed());
            }
            Err(e) => {
                println!("Failed to flush future: {:?}", e);
                self.previous_frame_end =
                    Some(vulkano::sync::now(self.logical.clone()).boxed());
            }
        }
    }

    pub fn window(&self) -> &Window {
        self.surface.window()
    }

    pub fn overlay_mut(&mut self) -> Option<&mut TextOverlay> {
        self.overlay.as_mut()
    }

    fn create_logical(
        physical: PhysicalDevice,
        surface: &Arc<Surface<Window>>,
    ) -> (Arc<Device>, QueuesIter) {
        let queue_family = physical
            .queue_families()
            .find(|&q| {
                q.supports_graphics()
                    && surface.is_supported(q).unwrap_or(false)
            })
            .unwrap();
        let device_ext = DeviceExtensions {
            khr_swapchain: true,
            ..DeviceExtensions::none()
        };
        let priority = 1.0;

        Device::new(
            physical,
            physical.supported_features(),
            &device_ext,
            [(queue_family, priority)].iter().cloned(),
        )
        .unwrap()
    }

    fn create_swapchain(
        physical: PhysicalDevice,
        surface: &Arc<Surface<Window>>,
        logical: Arc<Device>,
        queue: &Arc<Queue>,
    ) -> (Arc<Swapchain<Window>>, Vec<Arc<SwapchainImage<Window>>>) {
        let caps = surface.capabilities(physical).unwrap();
        let alpha = caps.supported_composite_alpha.iter().next().unwrap();
        let format = Format::B8G8R8A8Srgb;
        let dims: [u32; 2] = surface.window().inner_size().into();
        let layers = 1;
        let clipped = true;

        print!("supported alphas: ");
        caps.supported_composite_alpha
            .iter()
            .for_each(|a| print!("{:?} ", a));
        println!("\nsupported formats: {:?}", caps.supported_formats);
        println!("selected: {:?}, {:?}", alpha, format);

        Swapchain::new(
            logical.clone(),
            surface.clone(),
            caps.min_image_count,
            format,
            dims,
            layers,
            ImageUsage::color_attachment(),
            queue,
            SurfaceTransform::Identity,
            alpha,
            PresentMode::Fifo,
            FullscreenExclusive::Default,
            clipped,
            ColorSpace::SrgbNonLinear,
        )
        .unwrap()
    }

    fn create_render_pass(
        logical: Arc<Device>,
        format: Format,
    ) -> Arc<dyn RenderPassAbstract + Send + Sync> {
        Arc::new(
            vulkano::single_pass_renderpass!(
                logical.clone(),
                attachments: {
                    color: {
                        load: Clear,
                        store: Store,
                        format: format,
                        samples: 1,
                    },
                    depth: {
                        load: Clear,
                        store: DontCare,
                        format: Format::D16Unorm,
                        samples: 1,
                    }
                },
                pass: {
                    color: [color],
                    depth_stencil: {depth}
                }
            )
            .unwrap(),
        )
    }

    fn create_framebuffers(
        logical: Arc<Device>,
        images: &[Arc<SwapchainImage<Window>>],
        render_pass: Arc<dyn RenderPassAbstract + Send + Sync>,
    ) -> Vec<Arc<dyn FramebufferAbstract + Send + Sync>> {
        let depth_buffer = AttachmentImage::transient(
            logical.clone(),
            [
                images[0].dimensions()[0] as u32,
                images[0].dimensions()[1] as u32,
            ],
            Format::D16Unorm,
        )
        .unwrap();
        images
            .iter()
            .map(|image| {
                Arc::new(
                    Framebuffer::start(render_pass.clone())
                        .add(image.clone())
                        .unwrap()
                        .add(depth_buffer.clone())
                        .unwrap()
                        .build()
                        .unwrap(),
                ) as Arc<dyn FramebufferAbstract + Send + Sync>
            })
            .collect::<Vec<_>>()
    }

    fn create_pipeline(
        logical: Arc<Device>,
        vs: &obj_vs::Shader,
        fs: &obj_fs::Shader,
        dimensions: [f32; 2],
        render_pass: Arc<dyn RenderPassAbstract + Send + Sync>,
    ) -> Arc<dyn GraphicsPipelineAbstract + Send + Sync> {
        Arc::new(
            GraphicsPipeline::start()
                .vertex_input_single_buffer::<Vertex>()
                .vertex_shader(vs.main_entry_point(), ())
                .triangle_list()
                .cull_mode_back()
                .blend_alpha_blending()
                .viewports_dynamic_scissors_irrelevant(1)
                .viewports(std::iter::once(Viewport {
                    origin: [0.0, 0.0],
                    dimensions,
                    depth_range: 0.0..1.0,
                }))
                .fragment_shader(fs.main_entry_point(), ())
                .depth_stencil_simple_depth()
                .render_pass(Subpass::from(render_pass.clone(), 0).unwrap())
                .build(logical.clone())
                .unwrap(),
        )
    }

    fn recreate_swapchain(&mut self) {
        let dimensions: [u32; 2] = self.surface.window().inner_size().into();
        let (new_swapchain, new_images) =
            self.swapchain.recreate_with_dimensions(dimensions).unwrap();
        self.swapchain = new_swapchain;
        self.framebuffers = Renderer::create_framebuffers(
            self.logical.clone(),
            &new_images,
            self.render_pass.clone(),
        );
        let dimensions = [dimensions[0] as f32, dimensions[1] as f32];
        self.pipeline = Renderer::create_pipeline(
            self.logical.clone(),
            &self.vertex_shader,
            &self.frag_shader,
            dimensions,
            self.render_pass.clone(),
        );

        if let Some(overlay) = self.overlay.as_mut() {
            overlay.recreate_pipeline(dimensions);
        }

        self.swapchain_outdated = false;
    }
}

#[derive(Default, Copy, Clone)]
struct TextVertex {
    pos: [f32; 2],
    texture: [f32; 2], // v coord
}
vulkano::impl_vertex!(TextVertex, pos, texture);

pub struct TextOverlay {
    logical: Arc<Device>,

    render_pass: Arc<dyn RenderPassAbstract + Send + Sync>,
    pipeline: Arc<dyn GraphicsPipelineAbstract + Send + Sync>,
    dimensions: [f32; 2],

    vertex_shader: text_vs::Shader,
    frag_shader: text_fs::Shader,

    font: Font,
    vertex_buffer: Arc<CpuBufferPool<TextVertex>>,
    index_buffer: Arc<CpuBufferPool<u16>>,
    set: Arc<
        PersistentDescriptorSet<(
            ((), PersistentDescriptorSetImg<Arc<ImmutableImage<Format>>>),
            PersistentDescriptorSetSampler,
        )>,
    >,

    text_vertices: Vec<TextVertex>,
    text_indices: Vec<u16>,
    vertex_chunk: Arc<CpuBufferPoolChunk<TextVertex, Arc<StdMemoryPool>>>,
    index_chunk: Arc<CpuBufferPoolChunk<u16, Arc<StdMemoryPool>>>,
}

impl TextOverlay {
    pub fn new(
        logical: Arc<Device>,
        queue: Arc<Queue>,
        color_format: Format,
        dimensions: [f32; 2],
        mut font: Font,
    ) -> Self {
        let render_pass =
            Renderer::create_render_pass(logical.clone(), color_format);

        let vertex_shader = text_vs::Shader::load(logical.clone()).unwrap();
        let frag_shader = text_fs::Shader::load(logical.clone()).unwrap();

        let pipeline = TextOverlay::create_pipeline(
            logical.clone(),
            &vertex_shader,
            &frag_shader,
            dimensions,
            render_pass.clone(),
        );

        // Place font in texture buffer (as a single column of letters)
        let (texture_buffer, tex_future) = {
            ImmutableImage::from_iter(
                font.data.drain(..),
                Dimensions::Dim2d {
                    width: font.width,
                    height: font.length * font.height,
                },
                MipmapsCount::One,
                Format::R8Unorm,
                queue.clone(),
            )
            .unwrap()
        };
        tex_future.flush().unwrap();

        // Create vertex and index buffer pool for sending letter quads
        let vertex_buffer = Arc::new(CpuBufferPool::new(
            logical.clone(),
            BufferUsage::vertex_buffer(),
        ));
        let index_buffer = Arc::new(CpuBufferPool::new(
            logical.clone(),
            BufferUsage::index_buffer(),
        ));
        let vertex_chunk = Arc::new(vertex_buffer.chunk(vec![]).unwrap());
        let index_chunk = Arc::new(index_buffer.chunk(vec![]).unwrap());

        let sampler = Sampler::new(
            logical.clone(),
            Filter::Nearest,
            Filter::Nearest,
            MipmapMode::Nearest,
            SamplerAddressMode::Repeat,
            SamplerAddressMode::Repeat,
            SamplerAddressMode::Repeat,
            0.0,
            1.0,
            0.0,
            1.0,
        )
        .unwrap();

        let layout = pipeline.descriptor_set_layout(0).unwrap();
        let set = Arc::new(
            PersistentDescriptorSet::start(layout.clone())
                .add_sampled_image(texture_buffer, sampler.clone())
                .unwrap()
                .build()
                .unwrap(),
        );

        TextOverlay {
            logical,
            render_pass,
            pipeline,
            dimensions,
            vertex_shader,
            frag_shader,
            font,
            vertex_buffer,
            index_buffer,
            set,
            text_vertices: Vec::new(),
            text_indices: Vec::new(),
            vertex_chunk,
            index_chunk,
        }
    }

    pub fn recreate_pipeline(&mut self, dimensions: [f32; 2]) {
        self.dimensions = dimensions;
        self.pipeline = TextOverlay::create_pipeline(
            self.logical.clone(),
            &self.vertex_shader,
            &self.frag_shader,
            self.dimensions,
            self.render_pass.clone(),
        );
    }

    /* call for each string on the screen */
    pub fn add_text(&mut self, x: u32, y: u32, scale: f32, string: &str) {
        let nv = string.len() * 4;
        let ni = string.len() * 5;
        self.text_vertices.reserve(nv);
        self.text_indices.reserve(ni);

        let (w, h) = (self.dimensions[0], self.dimensions[1]);

        let mut x1 = x as f32;
        let (y1, y2) = (y as f32, (y + self.font.height) as f32 * scale);
        for c in string.chars() {
            let x2 = x1 + self.font.width as f32 * scale;
            let vx1 = x1 / (w / 2.0) - 1.0;
            let vx2 = x2 / (w / 2.0) - 1.0;
            let vy1 = y1 / (h / 2.0) - 1.0;
            let vy2 = y2 / (h / 2.0) - 1.0;
            x1 = x2;

            let c = c as u32 as f32;
            let ty1 = c / 256.0;
            let ty2 = (c + 1.0) / 256.0;

            self.text_vertices.extend_from_slice(&[
                TextVertex {
                    pos: [vx1, vy1],
                    texture: [0.0, ty1],
                },
                TextVertex {
                    pos: [vx1, vy2],
                    texture: [0.0, ty2],
                },
                TextVertex {
                    pos: [vx2, vy1],
                    texture: [1.0, ty1],
                },
                TextVertex {
                    pos: [vx2, vy2],
                    texture: [1.0, ty2],
                },
            ]);

            let last = (self.text_indices.len() as u16 / 5) * 4;
            self.text_indices.extend_from_slice(&[
                last,
                last + 1,
                last + 2,
                last + 3,
                0xffff, // primitive restart
            ]);
        }
    }

    /* call when all strings have been added clears added text for next frame
     * as well */
    pub fn load_text(&mut self) {
        self.vertex_chunk = Arc::new(
            self.vertex_buffer
                .chunk(self.text_vertices.drain(..))
                .unwrap(),
        );
        self.index_chunk = Arc::new(
            self.index_buffer
                .chunk(self.text_indices.drain(..))
                .unwrap(),
        );
    }

    fn create_pipeline(
        logical: Arc<Device>,
        vs: &text_vs::Shader,
        fs: &text_fs::Shader,
        dimensions: [f32; 2],
        render_pass: Arc<dyn RenderPassAbstract + Send + Sync>,
    ) -> Arc<dyn GraphicsPipelineAbstract + Send + Sync> {
        Arc::new(
            GraphicsPipeline::start()
                .vertex_input_single_buffer::<TextVertex>()
                .vertex_shader(vs.main_entry_point(), ())
                .triangle_strip()
                .primitive_restart(true)
                .blend_alpha_blending()
                .viewports_dynamic_scissors_irrelevant(1)
                .viewports(std::iter::once(Viewport {
                    origin: [0.0, 0.0],
                    dimensions,
                    depth_range: 0.0..1.0,
                }))
                .fragment_shader(fs.main_entry_point(), ())
                .render_pass(Subpass::from(render_pass.clone(), 0).unwrap())
                .build(logical.clone())
                .unwrap(),
        )
    }
}

mod obj_vs {
    vulkano_shaders::shader! {
        ty: "vertex", path: "obj/shader.vert.glsl",
    }
}
mod obj_fs {
    vulkano_shaders::shader! {
        ty: "fragment", path: "obj/shader.frag.glsl",
    }
}
mod text_vs {
    vulkano_shaders::shader! {
        ty: "vertex", path: "obj/text.vert.glsl",
    }
}
mod text_fs {
    vulkano_shaders::shader! {
        ty: "fragment", path: "obj/text.frag.glsl",
    }
}
