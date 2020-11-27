use cgmath::Matrix4;

#[allow(dead_code)] // read by GPU
pub struct Mvp {
    pub model: Matrix4<f32>,
    pub view: Matrix4<f32>,
    pub proj: Matrix4<f32>,
}

#[derive(Default, Copy, Clone)]
pub struct Vertex {
    pub pos: [f32; 3],
    pub texture: [f32; 2],
    pub normal: [f32; 3],
}
vulkano::impl_vertex!(Vertex, pos, texture, normal);

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
                        let vs: Vec<[i64; 3]> = fields[1..]
                            .iter()
                            .map(|f| {
                                let s: Vec<&str> = f.split("/").collect();
                                [
                                    s[0].parse().unwrap(),
                                    s.get(1)
                                        .unwrap_or(&"")
                                        .parse()
                                        .unwrap_or(1),
                                    s.get(2)
                                        .unwrap_or(&"")
                                        .parse()
                                        .unwrap_or(1),
                                ]
                            })
                            .collect();
                        let v0 = vs[0];
                        for (v1, v2) in vs[1..].iter().zip(vs[2..].iter()) {
                            f.push((v0[0], v0[1], v0[2]));
                            f.push((v1[0], v1[1], v1[2]));
                            f.push((v2[0], v2[1], v2[2]));
                        }
                    }
                    _ => {}
                }
            }
        }

        let n = f.len();
        let mut vertices: Vec<Vertex> = Vec::with_capacity(n);
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
                texture: if let Some(vt) = vt.get(vti) {
                    [vt[0], 1.0 - vt[1]]
                } else {
                    [0.0, 0.0]
                },
                normal: if let Some(vn) = vn.get(vni) {
                    *vn
                } else {
                    [1.0, 1.0, 1.0]
                },
            });
        }

        Ok(Obj {
            vertices,
            indices: (0..n as u32).collect(),
            texture,
        })
    }
}
