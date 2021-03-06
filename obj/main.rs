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
use types::{Font, Obj};
use view::Viewer;

const SCANCODE_ESC: ScanCode = 1;
const SCANCODE_LCTRL: ScanCode = 29;
const SCANCODE_LSHIFT: ScanCode = 42;
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

const REFRESH_OVERLAY_PERIOD: f32 = 1.0;

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
    let window = WindowBuilder::new().build(&el)?;

    let mut viewer = Viewer::new();
    let mut pressed: HashMap<ScanCode, bool> = HashMap::new();
    let mut last_frame = Instant::now();

    let font = std::fs::File::open("overlay.psf").map(|f| Font::from_psf2(f));

    let mut renderer = Renderer::new(window, obj);
    if let Ok(Ok(font)) = font {
        renderer = renderer.with_overlay(font);
    } else {
        eprintln!("overlay font failed to load")
    }

    let mut overlay_period = 0.0;
    let mut overlay_frames = 0;

    el.run(move |event, _, control_flow| match event {
        Event::WindowEvent {
            event: WindowEvent::CloseRequested,
            ..
        } => {
            *control_flow = ControlFlow::Exit;
        }
        Event::WindowEvent {
            event: WindowEvent::Focused(focused),
            ..
        } => {
            let grabbed = renderer.window().set_cursor_grab(focused).is_ok();
            renderer.window().set_cursor_visible(!grabbed);
        }
        Event::WindowEvent {
            event: WindowEvent::Resized(_),
            ..
        } => {
            renderer.swapchain_outdated();
        }
        Event::RedrawEventsCleared => {
            let now = Instant::now();
            let period =
                now.duration_since(last_frame).as_micros() as f32 / 1e6;
            last_frame = now;

            if let Some(overlay) = renderer.overlay_mut() {
                overlay_period += period;
                overlay_frames += 1;

                if overlay_period > REFRESH_OVERLAY_PERIOD {
                    let fps = (overlay_frames as f32 / overlay_period).round();
                    overlay.add_text(0, 0, 1.0, fps.to_string().as_str());
                    overlay.load_text();

                    overlay_period = 0.0;
                    overlay_frames = 0;
                }
            }

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
            viewer.boost(*pressed.get(&SCANCODE_LSHIFT).unwrap_or(&false));
            viewer.tick(period);

            renderer.redraw(viewer.model(), viewer.view());
        }
        Event::WindowEvent {
            event:
                WindowEvent::KeyboardInput {
                    input:
                        KeyboardInput {
                            scancode, state, ..
                        },
                    ..
                },
            ..
        } => {
            let quarter = std::f32::consts::PI / 2.0;
            if state == ElementState::Pressed {
                match scancode {
                    SCANCODE_ESC => *control_flow = ControlFlow::Exit,
                    SCANCODE_X => viewer.rotate_x(quarter),
                    SCANCODE_Y => viewer.rotate_y(quarter),
                    SCANCODE_Z => viewer.rotate_z(quarter),
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
