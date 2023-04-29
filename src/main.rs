
use glium;

use glium::{glutin, Surface};

#[derive(Copy, Clone)]
struct Vertex {
    position: [f32; 2],
}
glium::implement_vertex!(Vertex, position);

fn gen_vertex_shape<T>(display: &glium::Display, shape: Vec<T>) -> (glium::VertexBuffer<T>, glium::index::NoIndices) where T: Copy + glium::Vertex {
  let vertex_buffer = glium::VertexBuffer::new(display, &shape).unwrap();
  let indices = glium::index::NoIndices(glium::index::PrimitiveType::TrianglesList);
  (vertex_buffer, indices)
}

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

  //println!("display.get_max_viewport_dimensions() = {:?}", display.get_max_viewport_dimensions());
  println!("display.get_max_viewport_dimensions() = {:?}", display.get_max_viewport_dimensions());

  // Coordinates: float, -1.0,-1.0 is lower-left-most corner, 1.0, 1.0 is upper-right-most corner
  // Scales to match window dimensions, which currently is a fixed fraction of monitor size.
  let top_offset = 0.1;
  let shapes: Vec<(glium::VertexBuffer<Vertex>, glium::index::NoIndices)> = vec![
    gen_vertex_shape(&display, vec![
      // Left-most side
      Vertex { position: [ -1.0, -1.0 ] },
      Vertex { position: [ -1.0 + top_offset, 1.0 ] },
      Vertex { position: [ -1.0, -1.0 ] },
    ]),
    // gen_vertex_shape(&display, vec![
    //   // Upper-left center rectangle triangle
    //   Vertex { position: [ -1.0 + top_offset, -1.0 ] },
    //   Vertex { position: [ -1.0 + top_offset, 1.0 ] },
    //   Vertex { position: [ 1.0 - top_offset, 1.0 ] },
    // ]),
    gen_vertex_shape(&display, vec![
      // Bottom-Right center rectangle triangle
      Vertex { position: [ -1.0 + top_offset, -1.0 ] },
      Vertex { position: [ 1.0 - top_offset, 1.0 ] },
      Vertex { position: [ 1.0 - top_offset, -1.0 ] },
    ]),
    gen_vertex_shape(&display, vec![
      // Right-most side
      Vertex { position: [ 1.0, -1.0 ] },
      Vertex { position: [ 1.0 - top_offset, 1.0 ] },
      Vertex { position: [ 1.0, -1.0 ] },
    ]),
  ];
  // let shape = vec![
  //   // Left-most side
  //   Vertex { position: [ -1.0, -0.9 ] },
  //   Vertex { position: [ -1.0 + top_offset, 0.9 ] },
  //   Vertex { position: [ -1.0, -0.9 ] },

  //   // Upper-left center rectangle triangle
  //   Vertex { position: [ -1.0 + top_offset, -1.0 ] },
  //   Vertex { position: [ -1.0 + top_offset, 1.0 ] },
  //   Vertex { position: [ 1.0 - top_offset, 1.0 ] },
  //   // Bottom-Right center rectangle triangle
  //   Vertex { position: [ -1.0 + top_offset, -1.0 ] },
  //   Vertex { position: [ 1.0 - top_offset, 1.0 ] },
  //   Vertex { position: [ 1.0 - top_offset, -1.0 ] },

  //   // Right-most side
  //   // Vertex { position: [ 1.0, -1.0 ] },
  //   // Vertex { position: [ 1.0 - top_offset, 1.0 ] },
  //   // Vertex { position: [ 1.0, -1.0 ] },
  // ];

  // let vertex_buffer = glium::VertexBuffer::new(&display, &shape).unwrap();
  // let indices = glium::index::NoIndices(glium::index::PrimitiveType::TrianglesList);

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

  let frame_delay = std::time::Duration::from_millis(32);
  let mut last_frame_begin_time = std::time::Instant::now();
  let be_noisy = false;

  let mut have_set_win_pos = false;

  let mut frame_i = 0;
  let num_frames_to_count = 32; // approx 1sec worth of frames
  let mut frame_draw_total_work_time = std::time::Duration::from_millis(0);

  event_loop.run(move |event, _, control_flow| {

    if ! have_set_win_pos {
        let gl_win = display.gl_window();
        let gl_win = gl_win.window();
        if let Some(current_mon) = gl_win.current_monitor() {
          let mon_s = current_mon.size();
          
          let w = (mon_s.width as f64 * 0.75) as u32;
          let h = 128 as u32; // 96 for release l8ter
          let x = ( (mon_s.width/2) - (w/2) ) as i32;
          let y = ( mon_s.height - h ) as i32;

          gl_win.set_outer_position( glutin::dpi::Position::Physical( glutin::dpi::PhysicalPosition{x: x, y:y} ) );
          gl_win.set_inner_size( glutin::dpi::Size::Physical( glutin::dpi::PhysicalSize{width: w, height:h} ) );

          have_set_win_pos = true;

        }
      }

      frame_i += 1;

      //let next_frame_time = std::time::Instant::now() + std::time::Duration::from_nanos(16_666_667);
      let next_frame_time = last_frame_begin_time + frame_delay;

      // WaitUntil returns early if we get an event, which is common!
      // We loop until std::time::Instant::now() > next_frame_time
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

      loop {
        *control_flow = glutin::event_loop::ControlFlow::WaitUntil(next_frame_time); // hopefully an efficient wait and not just CPU spinning...
        if std::time::Instant::now() >= next_frame_time {
          break;
        }
        std::thread::sleep( next_frame_time - std::time::Instant::now() ); // cheating b/c we saw 100% cpu use
      }
      

      last_frame_begin_time = std::time::Instant::now();


      let mut target = display.draw();
      
      target.clear_color(0.0, 0.0, 0.0, 0.0);

      for (vertex_buffer, indices) in shapes.iter() {
        target.draw(&*vertex_buffer, &*indices, &program, &glium::uniforms::EmptyUniforms,
                    &Default::default()).unwrap();
      }
      
      //println!("Drew stuff!");

      if let Err(e) = target.finish() {
        println!("target.finish() e={:?}", e);
      }

      frame_draw_total_work_time += std::time::Instant::now() - last_frame_begin_time;

      if frame_i % num_frames_to_count == 0 {
        let per_frame_draw_ms = frame_draw_total_work_time.as_millis() as f64 / num_frames_to_count as f64;
        println!("{} frames took {} ms total to draw, or {} ms/frame", num_frames_to_count, frame_draw_total_work_time.as_millis(), per_frame_draw_ms);
        frame_draw_total_work_time = std::time::Duration::from_millis(0);
      }

  });

}
