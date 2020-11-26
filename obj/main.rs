use winit::event::{Event, WindowEvent};
use winit::event_loop::{ControlFlow, EventLoop};

mod render;
mod types;

use render::Renderer;
use types::Vertex;

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
            r.swapchain_outdated();
        }
        Event::RedrawEventsCleared => {
            r.redraw();
        }
        _ => {}
    });
}
