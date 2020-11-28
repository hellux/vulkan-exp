use cgmath::*;

const PI: Rad<f32> = Rad(std::f32::consts::PI);
const SPEED_STEP: f32 = 1.3;
const SPEED_LOSS: f32 = 0.9;
const SPEED_BOOST: f32 = 5.0;
const MOUSE_SENSITIVITY: f32 = 0.001;

pub struct Viewer {
    vel: Vector3<f32>,
    pos: Vector3<f32>,
    pitch: Rad<f32>,
    yaw: Rad<f32>,
    speed: f32,
    boost: bool,

    model_rotation: Vector3<f32>,
}

impl Viewer {
    pub fn new() -> Self {
        Viewer {
            vel: Vector3::new(0.0, 0.0, 0.0),
            pos: Vector3::new(0.0, 0.0, 3.0),
            pitch: Rad(0.0),
            yaw: Rad(0.0),
            speed: 1.0,
            boost: false,
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
        self.vel += self.vertical();
    }

    pub fn down(&mut self) {
        self.vel -= self.vertical();
    }

    pub fn boost(&mut self, b: bool) {
        self.boost = b;
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

    pub fn increase_speed(&mut self) {
        self.speed *= SPEED_STEP;
    }

    pub fn decrease_speed(&mut self) {
        self.speed /= SPEED_STEP;
    }

    fn speed(&self) -> f32 {
        let boost = if self.boost { SPEED_BOOST } else { 1.0 };
        self.speed * boost
    }

    fn dir(&self) -> Vector3<f32> {
        -Matrix3::from_angle_y(self.yaw)
            * Matrix3::from_angle_x(self.pitch)
            * Vector3::unit_z()
            * self.speed()
    }

    fn horizontal(&self, a: Rad<f32>) -> Vector3<f32> {
        let b = self.yaw + a;
        -self.speed() * Vector3::new(b.sin(), 0.0, b.cos())
    }

    fn vertical(&self) -> Vector3<f32> {
        Vector3::unit_y() * self.speed()
    }
}
