use cgmath::*;

const PI: Rad<f32> = Rad(std::f32::consts::PI);
const SPEED: f32 = 1.0;
const SPEED_LOSS: f32 = 0.9;
const MOUSE_SENSITIVITY: f32 = 0.005;

pub struct Viewer {
    vel: Vector3<f32>,
    pos: Vector3<f32>,
    pitch: Rad<f32>,
    yaw: Rad<f32>,

    model_rotation: Vector3<f32>,
}

impl Viewer {
    pub fn new() -> Self {
        Viewer {
            vel: Vector3::new(0.0, 0.0, 0.0),
            pos: Vector3::new(0.0, 0.0, 0.0),
            pitch: Rad(0.0),
            yaw: Rad(0.0),

            model_rotation: Vector3::new(0.0, 0.0, 0.0),
        }
    }

    pub fn view(&self) -> Matrix4<f32> {
        Matrix4::from_diagonal(Vector4::new(1.0, -1.0, 1.0, 1.0))
            * Matrix4::from_angle_x(-self.pitch)
            * Matrix4::from_angle_y(-self.yaw)
            * Matrix4::from_translation(-self.pos)
    }

    pub fn model(&self) -> Matrix4<f32> {
        Matrix4::from_angle_x(-Rad(self.model_rotation[0]))
            * Matrix4::from_angle_y(-Rad(self.model_rotation[1]))
            * Matrix4::from_angle_z(-Rad(self.model_rotation[2]))
    }

    pub fn forward(&mut self) {
        self.vel += self.dir();
    }

    pub fn backward(&mut self) {
        self.vel -= self.dir();
    }

    pub fn left(&mut self) {
        self.vel += self.horizontal(PI / 2.0);
    }

    pub fn right(&mut self) {
        self.vel += self.horizontal(-PI / 2.0);
    }

    pub fn up(&mut self) {
        self.vel += Viewer::vertical();
    }

    pub fn down(&mut self) {
        self.vel -= Viewer::vertical();
    }

    pub fn look(&mut self, dx: f32, dy: f32) {
        self.pitch -= Rad(dy) * MOUSE_SENSITIVITY;
        self.yaw -= Rad(dx) * MOUSE_SENSITIVITY;
    }

    pub fn tick(&mut self, period: f32) {
        self.vel *= SPEED_LOSS;
        self.pos += self.vel * period;
    }

    pub fn rotate_x(&mut self, a: f32) {
        self.model_rotation[0] += a;
    }

    pub fn rotate_y(&mut self, a: f32) {
        self.model_rotation[1] += a;
    }

    pub fn rotate_z(&mut self, a: f32) {
        self.model_rotation[2] += a;
    }

    fn dir(&self) -> Vector3<f32> {
        -Matrix3::from_angle_y(self.yaw)
            * Matrix3::from_angle_x(self.pitch)
            * Vector3::unit_z()
            * SPEED
    }

    fn horizontal(&self, a: Rad<f32>) -> Vector3<f32> {
        let b = self.yaw + a;
        -SPEED * Vector3::new(b.sin(), 0.0, b.cos())
    }

    fn vertical() -> Vector3<f32> {
        Vector3::unit_y() * SPEED
    }
}
