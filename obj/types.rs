use cgmath::Matrix4;

#[derive(Default, Copy, Clone)]
pub struct Vertex {
    pub pos: [f32; 3],
    pub texture: [f32; 2],
    pub normal: [f32; 3],
}
vulkano::impl_vertex!(Vertex, pos, texture, normal);

#[allow(dead_code)] // read by GPU
pub struct UniformBufferObject {
    pub model: Matrix4<f32>,
    pub view: Matrix4<f32>,
    pub proj: Matrix4<f32>,
}

pub struct Obj {
    pub vertices: Vec<Vertex>,
    pub indices: Vec<u32>,
    pub texture: Option<image::DynamicImage>,
}

impl Obj {
    pub fn new<R: std::io::BufRead>(
        obj_file: R,
        texture: Option<image::DynamicImage>,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        let mut v: Vec<[f32; 3]> = Vec::new();
        let mut vt: Vec<[f32; 2]> = Vec::new();
        let mut vn: Vec<[f32; 3]> = Vec::new();

        let mut f: Vec<(i64, i64, i64)> = Vec::new();
        let mut n: u32 = 0;

        for line in obj_file.lines() {
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
                                idxs[1].parse().unwrap_or(1),
                                idxs[2].parse().unwrap_or(1),
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
                (v.len() as i64 + vi) as usize
            } else {
                vi as usize - 1
            };
            let vti = if vti < 0 {
                (vt.len() as i64 + vti) as usize
            } else {
                vti as usize - 1
            };
            let vni = if vni < 0 {
                (vn.len() as i64 + vni) as usize
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

        Ok(Obj {
            vertices,
            indices: (0..n).collect(),
            texture,
        })
    }
}
