use std::sync::Arc;

use winit::dpi::PhysicalSize;
use winit::event::{Event, WindowEvent};
use winit::event_loop::{ControlFlow, EventLoop};
use winit::window::{Window, WindowBuilder};

use vulkano_win::VkSurfaceBuild;

use vulkano::buffer::cpu_pool::CpuBufferPool;
use vulkano::buffer::{BufferUsage, CpuAccessibleBuffer};
use vulkano::command_buffer::{AutoCommandBufferBuilder, DynamicState};
use vulkano::descriptor::descriptor_set::PersistentDescriptorSet;
use vulkano::descriptor::PipelineLayoutAbstract;
use vulkano::device::{Device, DeviceExtensions, Queue, QueuesIter};
use vulkano::format::Format;
use vulkano::framebuffer::{
    Framebuffer, FramebufferAbstract, RenderPassAbstract, Subpass,
};
use vulkano::image::attachment::AttachmentImage;
use vulkano::image::immutable::ImmutableImage;
use vulkano::image::{Dimensions, ImageUsage, SwapchainImage};
use vulkano::instance::{Instance, PhysicalDevice};
use vulkano::pipeline::viewport::Viewport;
use vulkano::pipeline::{GraphicsPipeline, GraphicsPipelineAbstract};
use vulkano::sampler::Sampler;
use vulkano::swapchain::{
    AcquireError, ColorSpace, FullscreenExclusive, PresentMode, Surface,
    SurfaceTransform, Swapchain,
};
use vulkano::sync::{FlushError, GpuFuture};

use cgmath::{Matrix4, Point3, Rad, Vector3};

#[derive(Default, Copy, Clone)]
struct Vertex {
    pos: [f32; 3],
    texture: [f32; 2],
    normal: [f32; 3],
}
vulkano::impl_vertex!(Vertex, pos, texture, normal);

#[allow(dead_code)] // read by GPU
struct UniformBufferObject {
    model: Matrix4<f32>,
    view: Matrix4<f32>,
    proj: Matrix4<f32>,
}

struct Renderer {
    surface: Arc<Surface<Window>>,
    logical: Arc<Device>,
    queue: Arc<Queue>,

    swapchain: Arc<Swapchain<Window>>,
    render_pass: Arc<dyn RenderPassAbstract + Send + Sync>,
    pipeline: Arc<dyn GraphicsPipelineAbstract + Send + Sync>,
    framebuffers: Vec<Arc<dyn FramebufferAbstract + Send + Sync>>,

    vertex_shader: vs::Shader,
    frag_shader: fs::Shader,
    sampler: Arc<Sampler>,

    uniform_buffer: CpuBufferPool<UniformBufferObject>,
    vertex_buffer: Arc<CpuAccessibleBuffer<[Vertex]>>,
    index_buffer: Arc<CpuAccessibleBuffer<[u32]>>,
    texture: Arc<ImmutableImage<Format>>,

    swapchain_outdated: bool,
    previous_frame_end: Option<Box<dyn GpuFuture>>,
}

impl Renderer {
    fn new(el: &EventLoop<()>) -> Renderer {
        let instance =
            Instance::new(None, &vulkano_win::required_extensions(), None)
                .unwrap();

        let physical = PhysicalDevice::enumerate(&instance).next().unwrap();
        println!("physical: {}, {:?}", physical.name(), physical.ty());

        let surface = WindowBuilder::new()
            .build_vk_surface(el, instance.clone())
            .unwrap();

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

        let vertex_shader = vs::Shader::load(logical.clone()).unwrap();
        let frag_shader = fs::Shader::load(logical.clone()).unwrap();

        let pipeline = Renderer::create_pipeline(
            logical.clone(),
            &vertex_shader,
            &frag_shader,
            &images,
            render_pass.clone(),
        );

        let framebuffers = Renderer::create_framebuffers(
            logical.clone(),
            &images,
            render_pass.clone(),
        );

        let uniform_buffer = CpuBufferPool::<UniformBufferObject>::new(
            logical.clone(),
            BufferUsage::all(),
        );

        let vertex_buffer = CpuAccessibleBuffer::from_iter(
            logical.clone(),
            BufferUsage::all(),
            false,
            std::iter::empty(),
        )
        .unwrap();

        let index_buffer = CpuAccessibleBuffer::from_iter(
            logical.clone(),
            BufferUsage::all(),
            false,
            std::iter::empty(),
        )
        .unwrap();

        let img: Vec<u8> = Vec::from([255, 255, 255, 255]);
        let (texture, tex_future) = ImmutableImage::from_iter(
            img.iter().cloned(),
            Dimensions::Dim2d {
                width: 1,
                height: 1,
            },
            swapchain.format(),
            queue.clone(),
        )
        .unwrap();

        let sampler = Sampler::simple_repeat_linear_no_mipmap(logical.clone());

        let previous_frame_end = Some(tex_future.boxed());

        Renderer {
            surface,
            logical,
            queue,
            swapchain,
            render_pass,
            pipeline,
            framebuffers,
            vertex_shader,
            frag_shader,
            sampler,
            uniform_buffer,
            vertex_buffer,
            index_buffer,
            texture,
            swapchain_outdated: false,
            previous_frame_end,
        }
    }

    fn load_to_buffers(
        &mut self,
        vertices: Vec<Vertex>,
        indices: Vec<u32>,
        texture: Option<image::DynamicImage>,
    ) {
        self.vertex_buffer = CpuAccessibleBuffer::from_iter(
            self.logical.clone(),
            BufferUsage::all(),
            false,
            vertices.iter().cloned(),
        )
        .unwrap();

        self.index_buffer = CpuAccessibleBuffer::from_iter(
            self.logical.clone(),
            BufferUsage::all(),
            false,
            indices.iter().cloned(),
        )
        .unwrap();

        if let Some(texture) = texture {
            let buf = texture.into_bgra8();
            let (width, height) = (buf.width(), buf.height());
            let (texture, tex_future) = ImmutableImage::from_iter(
                buf.into_raw().iter().cloned(),
                Dimensions::Dim2d { width, height },
                self.swapchain.format(),
                self.queue.clone(),
            )
            .unwrap();
            self.texture = texture;
            self.previous_frame_end = Some(tex_future.boxed());
        }
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
        let format = vulkano::format::Format::B8G8R8A8Srgb;
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
        vs: &vs::Shader,
        fs: &fs::Shader,
        images: &[Arc<SwapchainImage<Window>>],
        render_pass: Arc<dyn RenderPassAbstract + Send + Sync>,
    ) -> Arc<dyn GraphicsPipelineAbstract + Send + Sync> {
        Arc::new(
            GraphicsPipeline::start()
                .vertex_input_single_buffer::<Vertex>()
                .vertex_shader(vs.main_entry_point(), ())
                .triangle_list()
                .blend_alpha_blending()
                .viewports_dynamic_scissors_irrelevant(1)
                .viewports(std::iter::once(Viewport {
                    origin: [0.0, 0.0],
                    dimensions: [
                        images[0].dimensions()[0] as f32,
                        images[0].dimensions()[1] as f32,
                    ],
                    depth_range: 0.0..1.0,
                }))
                .fragment_shader(fs.main_entry_point(), ())
                .depth_stencil_simple_depth()
                .render_pass(Subpass::from(render_pass.clone(), 0).unwrap())
                .build(logical.clone())
                .unwrap(),
        )
    }

    fn create_ubo(aspect: f32) -> UniformBufferObject {
        let time = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_millis();
        let period = 7 * 1000;
        let angle = Rad(2.0 * 3.14 * (time % period) as f32 / period as f32);

        let eye = Point3::new(2.0, 1.0, 0.0);
        let center = Point3::new(0.0, 0.0, 0.0);
        let up = Vector3::new(0.0, -1.0, 0.0);

        UniformBufferObject {
            model: Matrix4::from_angle_y(angle),
            view: Matrix4::look_at(eye, center, up),
            proj: cgmath::perspective(Rad(1.0), aspect, 0.1, 10.0),
        }
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
        self.pipeline = Renderer::create_pipeline(
            self.logical.clone(),
            &self.vertex_shader,
            &self.frag_shader,
            &new_images,
            self.render_pass.clone(),
        );

        self.swapchain_outdated = false;
    }

    fn redraw(&mut self) {
        self.previous_frame_end.as_mut().unwrap().cleanup_finished();

        if self.swapchain_outdated {
            self.recreate_swapchain();
        }

        let PhysicalSize { width, height } =
            self.surface.window().inner_size();
        let aspect = width as f32 / height as f32;
        let ubo = Renderer::create_ubo(aspect);

        let uniform_subbuffer = self.uniform_buffer.next(ubo).unwrap();

        let layout = self.pipeline.descriptor_set_layout(0).unwrap();
        let set = Arc::new(
            PersistentDescriptorSet::start(layout.clone())
                .add_buffer(uniform_subbuffer)
                .unwrap()
                .add_sampled_image(self.texture.clone(), self.sampler.clone())
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

        let clear_values = vec![[0.0, 0.0, 0.0, 0.0].into(), 1f32.into()];
        let mut builder = AutoCommandBufferBuilder::primary_one_time_submit(
            self.logical.clone(),
            self.queue.family(),
        )
        .unwrap();
        builder
            .begin_render_pass(
                self.framebuffers[image_num].clone(),
                false,
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
            )
            .unwrap()
            .end_render_pass()
            .unwrap();
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
}

/* assume triangle faces, 3d coordinates*/
fn parse_obj<R: std::io::BufRead>(
    input: R,
) -> Result<(Vec<Vertex>, Vec<u32>), Box<dyn std::error::Error>> {
    let mut v: Vec<[f32; 3]> = Vec::new();
    let mut vt: Vec<[f32; 2]> = Vec::new();
    let mut vn: Vec<[f32; 3]> = Vec::new();

    let mut f: Vec<(i64, i64, i64)> = Vec::new();
    let mut n: u32 = 0;

    for line in input.lines() {
        let line = line?;
        let fields: Vec<&str> = line.split_whitespace().collect();
        if fields.len() > 0 {
            match fields[0] {
                "v" => {
                    v.push([
                        fields[1].parse()?,
                        fields[2].parse()?,
                        fields[3].parse()?,
                    ]);
                }
                "vt" => {
                    vt.push([fields[1].parse()?, fields[2].parse()?]);
                }
                "vn" => {
                    vn.push([
                        fields[1].parse()?,
                        fields[2].parse()?,
                        fields[3].parse()?,
                    ]);
                }
                "f" => {
                    for vertex in fields[1..].iter() {
                        let idxs: Vec<&str> = vertex.split("/").collect();
                        f.push((
                            idxs[0].parse()?,
                            idxs[1].parse().unwrap_or_default(),
                            idxs[2].parse().unwrap_or_default(),
                        ));
                        n += 1;
                    }
                }
                _ => {}
            }
        }
    }

    let mut vertices: Vec<Vertex> = Vec::with_capacity(n as usize);
    for (vi, vti, vni) in f {
        let vi = if vi < 0 {
            v.len() + vi as usize
        } else {
            vi as usize - 1
        };
        let vti = if vti < 0 {
            vt.len() + vti as usize
        } else {
            vti as usize - 1
        };
        let vni = if vni < 0 {
            vn.len() + vni as usize
        } else {
            vni as usize - 1
        };

        vertices.push(Vertex {
            pos: v[vi],
            texture: if vt.is_empty() {
                [0.0, 0.0]
            } else {
                [vt[vti][0], 1.0 - vt[vti][1]]
            },
            normal: vn[vni],
        });
    }

    Ok((vertices, (0..n).collect()))
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    if !(2 <= args.len() && args.len() <= 3) {
        eprintln!("usage: <obj> [texture]");
        std::process::exit(1);
    }
    let input = std::fs::File::open(&args[1])?;
    let input = std::io::BufReader::new(input);
    let (vertices, indices) = parse_obj(input)?;

    let texture = match args.get(2) {
        Some(fname) => Some(image::open(fname)?),
        _ => None,
    };

    let el = EventLoop::new();
    let mut r = Renderer::new(&el);
    r.load_to_buffers(vertices, indices, texture);

    el.run(move |event, _, control_flow| match event {
        Event::WindowEvent {
            event: WindowEvent::CloseRequested,
            ..
        } => {
            *control_flow = ControlFlow::Exit;
        }
        Event::WindowEvent {
            event: WindowEvent::Resized(_),
            ..
        } => {
            r.swapchain_outdated = true;
        }
        Event::RedrawEventsCleared => {
            r.redraw();
        }
        _ => (),
    });
}

mod vs {
    vulkano_shaders::shader! {
        ty: "vertex", path: "obj/shader.vert.glsl",
    }
}
mod fs {
    vulkano_shaders::shader! {
        ty: "fragment", path: "obj/shader.frag.glsl",
    }
}
