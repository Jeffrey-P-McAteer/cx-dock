
use glium;

use glium::{glutin, Surface};

fn main() {

  let res = std::process::Command::new("sh")
    .args(&[
      "-c", " pgrep compton || (i3-msg exec compton && echo Used i3-exec to spawn compton) || ( ( compton & ) & echo Double-forked compton )  "
    ])
    .output();
  if let Err(e) = res {
    println!("Error running compositor shell check, e={:?}", e);
  }

  let event_loop = glutin::event_loop::EventLoop::new();
  
  let wb = glutin::window::WindowBuilder::new()
    .with_transparent(true)
    .with_decorations(false)
    .with_always_on_top(true);
    
  let cb = glutin::ContextBuilder::new()
    .with_gl(glutin::GlRequest::Latest);

  let display = glium::Display::new(wb, cb, &event_loop).unwrap();

  #[derive(Copy, Clone)]
  struct Vertex {
      position: [f32; 2],
  }

  glium::implement_vertex!(Vertex, position);

  let vertex1 = Vertex { position: [-0.5, -0.5] };
  let vertex2 = Vertex { position: [ 0.0,  0.5] };
  let vertex3 = Vertex { position: [ 0.5, -0.25] };
  let shape = vec![vertex1, vertex2, vertex3];

  let vertex_buffer = glium::VertexBuffer::new(&display, &shape).unwrap();
  let indices = glium::index::NoIndices(glium::index::PrimitiveType::TrianglesList);

  let vertex_shader_src = r#"
      #version 140
      in vec2 position;
      void main() {
          gl_Position = vec4(position, 0.0, 1.0);
      }
  "#;

  let fragment_shader_src = r#"
      #version 140
      out vec4 color;
      void main() {
          color = vec4(1.0, 0.0, 0.0, 1.0);
      }
  "#;

  let program = glium::Program::from_source(&display, vertex_shader_src, fragment_shader_src, None).unwrap();

  let frame_delay = std::time::Duration::from_millis(14);
  let be_noisy = false;

  event_loop.run(move |event, _, control_flow| {
      //let next_frame_time = std::time::Instant::now() + std::time::Duration::from_nanos(16_666_667);
      let next_frame_time = std::time::Instant::now() + frame_delay;
      *control_flow = glutin::event_loop::ControlFlow::WaitUntil(next_frame_time);

      match event {
          glutin::event::Event::WindowEvent { event, .. } => match event {
              glutin::event::WindowEvent::CloseRequested => {
                  println!("Exiting!");
                  *control_flow = glutin::event_loop::ControlFlow::Exit;
                  return;
              },
              unk_window_event => {
                if be_noisy {
                  println!("unk_window_event={:?}", unk_window_event);
                }
              },
          },
          glutin::event::Event::NewEvents(cause) => match cause {
              glutin::event::StartCause::ResumeTimeReached { .. } => {
                println!("ResumeTimeReached!");
              },
              glutin::event::StartCause::Init => {
                println!("Init!");
              },
              unk_new_event_cause => {
                if be_noisy {
                  println!("unk_new_event_cause={:?}", unk_new_event_cause);
                }
              },
          },
          unk_event => {
            if be_noisy {
              println!("unk_event={:?}", unk_event);
            }
          },
      }

      let mut target = display.draw();
      
      target.clear_color(0.0, 0.0, 0.0, 0.0);

      target.draw(&vertex_buffer, &indices, &program, &glium::uniforms::EmptyUniforms,
                  &Default::default()).unwrap();
      
      //println!("Drew stuff!");

      if let Err(e) = target.finish() {
        println!("target.finish() e={:?}", e);
      }

  });

}
