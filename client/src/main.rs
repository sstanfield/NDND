use std::io::prelude::*;
use std::io::ErrorKind;
use std::os::unix::net::UnixStream;

extern crate clap;
use clap::{App, Arg};
//use liner::{keymap, Buffer, ColorClosure, Context, Prompt};
use liner::{Context, Prompt};

use ahndn_client::types::*;

fn main() -> std::io::Result<()> {
    let matches = App::new("NDN Status Client")
        .version("0.1")
        .author("Steven Stanfield")
        .about("Connects to local AHNDN agent")
        .arg(
            Arg::with_name("socket")
                .short("s")
                .long("socket")
                //.value_name("FILE")
                .help("Sets the unix file socket for the agent")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("piers")
                .short("p")
                .long("piers")
                .help("List the piers of the agent."),
        )
        .arg(
            Arg::with_name("v")
                .short("v")
                .multiple(true)
                .help("Sets the level of verbosity"),
        )
        .get_matches();

    let mut buffer: [u8; 1024] = [0; 1024];
    let socket_name = matches.value_of("socket").unwrap_or("/tmp/ah");
    let mut stream = UnixStream::connect(socket_name)?;
    stream.set_nonblocking(true)?;

    if matches.is_present("piers") {
        writeln!(stream, "piers")?;
        stream.set_nonblocking(false)?;
        let mut json: String = String::new();
        while let Ok(n) = stream.read(&mut buffer[..]) {
            json.push_str(&String::from_utf8_lossy(&buffer[..n]).trim_matches(char::from(0)));
            stream.set_nonblocking(true)?;
            if n == 0 {
                break;
            }
        }
        println!("{}", json);
        return Ok(());
    }
    let mut con = Context::new();
    loop {
        match con.read_line(Prompt::from("agent> "), None) {
            Ok(input) => {
                if let Err(err) = con.history.push(&input[..]) {
                    eprintln!("Warning: failed to save history: {}", err);
                }
                let input = input.trim();
                if input.starts_with("stats") {
                    writeln!(stream, "status")?;
                } else if input.starts_with("pier-stats") {
                    let mut parts = input.split_whitespace();
                    parts.next();
                    if let Some(pier) = parts.next() {
                        writeln!(stream, "pier-status {}", pier)?;
                    }
                } else {
                    writeln!(stream, "{}", input)?;
                }
                stream.set_nonblocking(false)?;
                let mut json: String = String::new();
                while let Ok(n) = stream.read(&mut buffer[..]) {
                    json.push_str(
                        &String::from_utf8_lossy(&buffer[..n]).trim_matches(char::from(0)),
                    );
                    stream.set_nonblocking(true)?;
                    if n == 0 {
                        break;
                    }
                }
                if json.starts_with("ERROR") {
                    println!("Agent error: {}", json);
                } else if input == "status" || input.starts_with("pier-status") {
                    let faces: Vec<Face> = serde_json::from_str(&json)?;
                    for face in faces.iter() {
                        println!(
                            "{}\t{}\t{}\t{}/{}",
                            face.id,
                            face.link_type,
                            face.face_scope,
                            face.local_uri,
                            face.remote_uri
                        );
                    }
                } else if input == "piers" {
                    let piers: Vec<Pier> = serde_json::from_str(&json)?;
                    for pier in piers.iter() {
                        println!(
                            "{}: {} ({}) {}:{}",
                            pier.id, pier.prefix, pier.face_id, pier.ip, pier.port
                        );
                    }
                } else if input.starts_with("stats") || input.starts_with("pier-stats") {
                    let mut parts = input.split_whitespace();
                    parts.next();
                    if input.starts_with("pier-stats") {
                        parts.next();
                    }
                    if let Some(face_id_str) = parts.next() {
                        if let Ok(face_id) = u64::from_str_radix(face_id_str, 10) {
                            let faces: Vec<Face> = serde_json::from_str(&json)?;
                            for face in faces.iter() {
                                if face.id == face_id {
                                    println!(
                                        "{}\t{}\t{}\t{}/{}",
                                        face.id,
                                        face.link_type,
                                        face.face_scope,
                                        face.local_uri,
                                        face.remote_uri
                                    );
                                    println!(
                                        "    interests {}/{}",
                                        face.in_interests, face.out_interests
                                    );
                                    println!("    bytes     {}/{}", face.in_bytes, face.out_bytes);
                                    println!("    data      {}/{}", face.in_data, face.out_data);
                                    println!("    nacks     {}/{}", face.in_nacks, face.out_nacks);
                                }
                            }
                        }
                    } else {
                        eprintln!("Error, stats takes one number, the face id");
                        continue;
                    }
                } else {
                    println!("{}", json);
                }
            }
            Err(err) => match err.kind() {
                ErrorKind::UnexpectedEof => {
                    println!("Exiting...");
                    writeln!(stream, "exit")?;
                    break;
                }
                ErrorKind::Interrupted => {
                    println!("Interupted...");
                    writeln!(stream, "exit")?;
                    break;
                }
                _ => eprintln!("Error on input: {}", err),
            },
        };
    }
    Ok(())
}
