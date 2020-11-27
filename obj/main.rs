use std::collections::HashMap;
use std::time::Instant;

use winit::event::{
    DeviceEvent, ElementState, Event, KeyboardInput, ScanCode, WindowEvent,
};
use winit::event_loop::{ControlFlow, EventLoop};
use winit::window::WindowBuilder;

mod render;
mod types;
mod view;
use render::Renderer;
use types::Obj;
use view::Viewer;

const SCANCODE_ESC: ScanCode = 1;
const SCANCODE_LCTRL: ScanCode = 29;
const SCANCODE_SPACE: ScanCode = 57;
const SCANCODE_PLUS: ScanCode = 78;
const SCANCODE_MINUS: ScanCode = 74;
const SCANCODE_W: ScanCode = 17;
const SCANCODE_A: ScanCode = 30;
const SCANCODE_S: ScanCode = 31;
const SCANCODE_D: ScanCode = 32;
const SCANCODE_X: ScanCode = 45;
const SCANCODE_Y: ScanCode = 21;
const SCANCODE_Z: ScanCode = 44;

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
    let mut obj = Some(Obj::new(obj_file, texture)?);

    let el = EventLoop::new();
    let mut window = Some(WindowBuilder::new().build(&el).unwrap());

    let mut viewer = Viewer::new();
    let mut pressed: HashMap<ScanCode, bool> = HashMap::new();
    let mut last_frame = Instant::now();

    let mut renderer: Option<Renderer> = None;

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
            if let Some(r) = &mut renderer {
                r.swapchain_outdated();
            } else {
                let o = std::mem::replace(&mut obj, None).unwrap();
                let w = std::mem::replace(&mut window, None).unwrap();
                w.set_cursor_grab(true).unwrap();
                w.set_cursor_visible(false);
                renderer = Some(Renderer::new(w, o));
            }
        }
        Event::RedrawEventsCleared => {
            if let Some(r) = &mut renderer {
                r.redraw(viewer.model(), viewer.view());
            }

            let now = Instant::now();
            let period =
                now.duration_since(last_frame).as_micros() as f32 / 1e6;
            if *pressed.get(&SCANCODE_W).unwrap_or(&false) {
                viewer.forward();
            }
            if *pressed.get(&SCANCODE_A).unwrap_or(&false) {
                viewer.left();
            }
            if *pressed.get(&SCANCODE_S).unwrap_or(&false) {
                viewer.backward();
            }
            if *pressed.get(&SCANCODE_D).unwrap_or(&false) {
                viewer.right();
            }
            if *pressed.get(&SCANCODE_SPACE).unwrap_or(&false) {
                viewer.up();
            }
            if *pressed.get(&SCANCODE_LCTRL).unwrap_or(&false) {
                viewer.down();
            }
            viewer.tick(period);

            last_frame = now;
        }
        Event::WindowEvent {
            event:
                WindowEvent::KeyboardInput {
                    input:
                        KeyboardInput {
                            scancode,
                            state,
                            modifiers,
                            ..
                        },
                    ..
                },
            ..
        } => {
            let quarter = std::f32::consts::PI / 2.0;
            let dir = if modifiers.shift() { -1.0 } else { 1.0 };
            if state == ElementState::Pressed {
                match scancode {
                    SCANCODE_ESC => *control_flow = ControlFlow::Exit,
                    SCANCODE_X => viewer.rotate_x(quarter * dir),
                    SCANCODE_Y => viewer.rotate_y(quarter * dir),
                    SCANCODE_Z => viewer.rotate_z(quarter * dir),
                    SCANCODE_PLUS => viewer.increase_speed(),
                    SCANCODE_MINUS => viewer.decrease_speed(),
                    _ => {}
                }
            }
            pressed.insert(scancode, state == ElementState::Pressed);
        }
        Event::DeviceEvent {
            event: DeviceEvent::MouseMotion { delta: (dx, dy) },
            ..
        } => viewer.look(dx as f32, dy as f32),
        _ => {}
    });
}
