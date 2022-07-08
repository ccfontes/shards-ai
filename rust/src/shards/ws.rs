use crate::{
  core::{activate_blocking, do_blocking, registerShard},
  types::{
    ClonedVar, Context, ParamVar, Parameters, Type, Types, Var, ANYS_TYPES, FRAG_CC, NONE_TYPES,
    STRING_TYPES,
  },
  Shard,
};
use std::net::TcpStream;
use std::rc::Rc;
use std::thread::spawn;
use tungstenite::{
  accept, handshake::client::Response, stream::MaybeTlsStream, Message, WebSocket,
};

/*
"wss://xyz.xyz" (WS.Client) = .xyz-client

(WS.ReadString .xyz-client) ...
"Ok" (WS.WriteString .xyz-client)
*/

static WS_CLIENT_TYPE: Type = Type::object(FRAG_CC, 2004042604); // 'wsCl'
static WS_CLIENT_SLICE: &'static [Type] = &[WS_CLIENT_TYPE];
static WS_CLIENT_VAR: Type = Type::context_variable(WS_CLIENT_SLICE);

lazy_static! {
  static ref WS_CLIENT_VEC: Types = vec![WS_CLIENT_TYPE];
  static ref WS_CLIENT_VAR_VEC: Types = vec![WS_CLIENT_VAR];
  static ref WS_CLIENT_USER_PARAMS: Parameters = vec![(
    cstr!("Client"),
    cstr!("The WebSocket client instance."),
    &WS_CLIENT_VAR_VEC[..],
  )
    .into(),];
}

#[derive(Default)]
struct Client {
  ws: Rc<Option<WebSocket<MaybeTlsStream<TcpStream>>>>,
}

impl Shard for Client {
  fn registerName() -> &'static str
  where
    Self: Sized,
  {
    cstr!("WS.Client")
  }

  fn hash() -> u32
  where
    Self: Sized,
  {
    compile_time_crc32::crc32!("WS.Client-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "WS.Client"
  }

  fn inputTypes(&mut self) -> &Types {
    &STRING_TYPES
  }

  fn outputTypes(&mut self) -> &Types {
    &WS_CLIENT_VEC
  }

  fn activate(&mut self, context: &Context, input: &Var) -> Result<Var, &str> {
    Ok(do_blocking(context, || {
      let addr: &str = input.try_into()?;
      let ws = tungstenite::client::connect(addr).map_err(|e| {
        shlog!("{}", e);
        "Failed to connect to server."
      })?;
      // we need to keep this alive here, otherwise it will be dropped at the end of this function
      self.ws = Rc::new(Some(ws.0));
      let var_ws = Var::new_object(&self.ws, &WS_CLIENT_TYPE);
      Ok(var_ws)
    }))
  }
}

#[derive(Default)]
struct ClientUser {
  ws: Option<Rc<Option<WebSocket<MaybeTlsStream<TcpStream>>>>>,
  instance: ParamVar,
}

impl ClientUser {
  fn set_param(&mut self, index: i32, value: &Var) {
    match index {
      0 => self.instance.set_param(value),
      _ => panic!("Invalid index {}", index),
    }
  }

  fn get_param(&self, index: i32) -> Var {
    match index {
      0 => self.instance.get_param(),
      _ => panic!("Invalid index {}", index),
    }
  }

  fn warmup(&mut self, context: &Context) -> Result<(), &str> {
    self.instance.warmup(context);
    Ok(())
  }

  fn cleanup(&mut self) -> Result<(), &str> {
    self.instance.cleanup();
    Ok(())
  }

  fn activate(&mut self) -> Result<&mut WebSocket<MaybeTlsStream<TcpStream>>, &str> {
    if self.ws.is_none() {
      let ws = self.instance.get();
      self.ws = Some(Var::from_object_as_clone(ws, &WS_CLIENT_TYPE)?);
    }

    let ws = Var::get_mut_from_clone(&self.ws)?;

    Ok(ws)
  }
}

#[derive(Default)]
struct ReadString {
  client: ClientUser,
  text: ClonedVar,
}

impl Shard for ReadString {
  fn registerName() -> &'static str
  where
    Self: Sized,
  {
    cstr!("WS.ReadString")
  }

  fn hash() -> u32
  where
    Self: Sized,
  {
    compile_time_crc32::crc32!("WS.ReadString-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "WS.ReadString"
  }

  fn inputTypes(&mut self) -> &Types {
    &NONE_TYPES
  }

  fn outputTypes(&mut self) -> &Types {
    &STRING_TYPES
  }

  fn parameters(&mut self) -> Option<&Parameters> {
    Some(&WS_CLIENT_USER_PARAMS)
  }

  fn setParam(&mut self, index: i32, value: &Var) -> Result<(), &str> {
    self.client.set_param(index, value);
    Ok(())
  }

  fn getParam(&mut self, index: i32) -> Var {
    self.client.get_param(index)
  }

  fn warmup(&mut self, context: &Context) -> Result<(), &str> {
    self.client.warmup(context)
  }

  fn cleanup(&mut self) -> Result<(), &str> {
    self.client.cleanup()
  }

  fn activate(&mut self, context: &Context, _input: &Var) -> Result<Var, &str> {
    let ws = self.client.activate()?;
    Ok(do_blocking(context, || {
      let msg = ws.read_message().map_err(|e| {
        shlog!("{}", e);
        "Failed to read message."
      })?;

      match msg {
        Message::Text(text) => {
          self.text = text.into();
        }
        _ => return Err("Invalid message type."),
      }
      Ok(self.text.0)
    }))
  }
}

#[derive(Default)]
struct SendString {
  client: ClientUser,
}

impl Shard for SendString {
  fn registerName() -> &'static str
  where
    Self: Sized,
  {
    cstr!("WS.SendString")
  }

  fn hash() -> u32
  where
    Self: Sized,
  {
    compile_time_crc32::crc32!("WS.SendString-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "WS.SendString"
  }

  fn inputTypes(&mut self) -> &Types {
    &STRING_TYPES
  }

  fn outputTypes(&mut self) -> &Types {
    &STRING_TYPES
  }

  fn parameters(&mut self) -> Option<&Parameters> {
    Some(&WS_CLIENT_USER_PARAMS)
  }

  fn setParam(&mut self, index: i32, value: &Var) -> Result<(), &str> {
    self.client.set_param(index, value);
    Ok(())
  }

  fn getParam(&mut self, index: i32) -> Var {
    self.client.get_param(index)
  }

  fn warmup(&mut self, context: &Context) -> Result<(), &str> {
    self.client.warmup(context)
  }

  fn cleanup(&mut self) -> Result<(), &str> {
    self.client.cleanup()
  }

  fn activate(&mut self, context: &Context, input: &Var) -> Result<Var, &str> {
    let ws = self.client.activate()?;
    Ok(do_blocking(context, || {
      let msg: &str = input.try_into()?;

      ws.write_message(Message::Text(msg.to_string()))
        .map_err(|e| {
          shlog!("{}", e);
          "Failed to send message."
        })?;

      Ok(*input)
    }))
  }
}

pub fn registerShards() {
  registerShard::<Client>();
  registerShard::<ReadString>();
  registerShard::<SendString>();
}
