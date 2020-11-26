use winit::event::{Event, WindowEvent};
use winit::event_loop::{ControlFlow, EventLoop};

mod render;
mod types;

use render::Renderer;
use types::Obj;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    if !(2 <= args.len() && args.len() <= 3) {
        eprintln!("usage: <obj> [texture]");
        std::process::exit(1);
    }

    let obj_file = std::fs::File::open(&args[1])?;
    let obj_file = std::io::BufReader::new(obj_file);
    let texture = match args.get(2) {
        Some(fname) => Some(image::open(fname)?),
        _ => None,
    };
    let obj = Obj::new(obj_file, texture)?;

    let el = EventLoop::new();
    let mut r = Renderer::new(&el);
    r.load_to_buffers(obj);

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
            r.swapchain_outdated();
        }
        Event::RedrawEventsCleared => {
            r.redraw();
        }
        _ => {}
    });
}
