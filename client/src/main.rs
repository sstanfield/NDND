use std::io::prelude::*;
use std::io::ErrorKind;
use std::os::unix::net::UnixStream;

extern crate clap;
use clap::{App, Arg};
//use liner::{keymap, Buffer, ColorClosure, Context, Prompt};
use liner::{Context, Prompt};

use ahndn_client::types::*;

fn get_json(stream: &mut UnixStream) -> std::io::Result<String> {
    let mut buffer: [u8; 1024] = [0; 1024];
    stream.set_nonblocking(false)?;
    let mut json: String = String::new();
    while let Ok(n) = stream.read(&mut buffer[..]) {
        json.push_str(&String::from_utf8_lossy(&buffer[..n]).trim_matches(char::from(0)));
        stream.set_nonblocking(true)?;
        if n == 0 {
            break;
        }
    }
    Ok(json)
}

fn print_json(input: &str, json: &str) -> std::io::Result<()> {
    if json.starts_with("ERROR") {
        println!("Agent error: {}", json);
    } else if input == "status" || input.starts_with("pier-status") {
        let faces: Vec<Face> = serde_json::from_str(&json)?;
        for face in faces.iter() {
            println!(
                "{}\t{}\t{}\t{}/{}",
                face.id, face.link_type, face.face_scope, face.local_uri, face.remote_uri
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
                        println!("    interests {}/{}", face.in_interests, face.out_interests);
                        println!("    bytes     {}/{}", face.in_bytes, face.out_bytes);
                        println!("    data      {}/{}", face.in_data, face.out_data);
                        println!("    nacks     {}/{}", face.in_nacks, face.out_nacks);
                    }
                }
            }
        } else {
            eprintln!("Error, stats takes one number, the face id");
        }
    } else {
        println!("{}", json);
    }
    Ok(())
}

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
                .long("piers")
                .help("List the piers of the agent."),
        )
        .arg(
            Arg::with_name("status")
                .long("status")
                .help("List the status of the local NDN."),
        )
        .arg(
            Arg::with_name("pier-status")
                .long("pier-status")
                .help("List the status of a pier (by id from --piers).")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("stats")
                .long("stats")
                .help("Detailed stats for the given local face id.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("pier-stats")
                .long("pier-stats")
                .help("Detailed stats for a pier's face id.")
                .takes_value(true)
                .multiple(true)
                .number_of_values(2),
        )
        .get_matches();

    let socket_name = matches.value_of("socket").unwrap_or("/tmp/ah");
    let mut stream = UnixStream::connect(socket_name)?;
    stream.set_nonblocking(true)?;

    let mut repl = true;
    if matches.is_present("piers") {
        writeln!(stream, "piers")?;
        let json = get_json(&mut stream)?;
        print_json("piers", &json)?;
        repl = false;
    }
    if matches.is_present("status") {
        writeln!(stream, "status")?;
        let json = get_json(&mut stream)?;
        print_json("status", &json)?;
        repl = false;
    }
    if matches.is_present("pier-status") {
        let pier = matches
            .value_of("pier-status")
            .unwrap_or("0")
            .parse::<u64>()
            .map_err(|e| {
                std::io::Error::new(
                    std::io::ErrorKind::Other,
                    format!("Pier not a number {}", e),
                )
            })?;
        writeln!(stream, "pier-status {}", pier)?;
        let json = get_json(&mut stream)?;
        print_json("pier-status", &json)?;
        repl = false;
    }
    if matches.is_present("stats") {
        let face = matches
            .value_of("stats")
            .unwrap_or("0")
            .parse::<u64>()
            .map_err(|e| {
                std::io::Error::new(
                    std::io::ErrorKind::Other,
                    format!("Stats face id not a number {}", e),
                )
            })?;
        writeln!(stream, "status")?;
        let json = get_json(&mut stream)?;
        print_json(&format!("stats {}", face), &json)?;
        repl = false;
    }
    if matches.is_present("pier-stats") {
        if let Some(mut vals) = matches.values_of("pier-stats") {
            let pier = vals.next().unwrap_or("X").parse::<u64>().map_err(|e| {
                std::io::Error::new(
                    std::io::ErrorKind::Other,
                    format!("Pier-stats pier id not a number {}", e),
                )
            })?;
            let face = vals.next().unwrap_or("X").parse::<u64>().map_err(|e| {
                std::io::Error::new(
                    std::io::ErrorKind::Other,
                    format!("Pier-stats face id not a number {}", e),
                )
            })?;
            writeln!(stream, "pier-status {}", pier)?;
            let json = get_json(&mut stream)?;
            print_json(&format!("pier-stats {} {}", pier, face), &json)?;
        }
        repl = false;
    }
    if !repl {
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
                let json = get_json(&mut stream)?;
                print_json(input, &json)?;
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
