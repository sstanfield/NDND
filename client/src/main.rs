use std::io::prelude::*;
use std::io::ErrorKind;
use std::os::unix::net::UnixStream;

extern crate clap;
use clap::{App, Arg};
//use liner::{keymap, Buffer, ColorClosure, Context, Prompt};
use liner::{Context, Prompt};

use ahndn_client::parse::*;
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

fn print_face(face: &Face) {
    println!(
        "{}\t{}\t{}\t{} to {}",
        face.id, face.link_type, face.face_scope, face.local_uri, face.remote_uri
    );
    print!("\tRoutes: ");
    for route in &face.routes {
        print!("{}, ", route.name);
    }
    println!();
}

fn print_status(json: &str) -> std::io::Result<()> {
    let faces: Vec<Face> = serde_json::from_str(&json)?;
    for face in faces.iter() {
        print_face(face);
    }
    Ok(())
}

fn print_stats(face_id: u64, json: &str) -> std::io::Result<()> {
    let faces: Vec<Face> = serde_json::from_str(&json)?;
    for face in faces.iter() {
        if face.id == face_id {
            print_face(face);
            println!("    interests {}/{}", face.in_interests, face.out_interests);
            println!("    bytes     {}/{}", face.in_bytes, face.out_bytes);
            println!("    data      {}/{}", face.in_data, face.out_data);
            println!("    nacks     {}/{}", face.in_nacks, face.out_nacks);
        }
    }
    Ok(())
}

fn print_stats_by_route(route_name: &str, json: &str) -> std::io::Result<()> {
    let faces: Vec<Face> = serde_json::from_str(&json)?;
    for face in faces.iter() {
        for route in &face.routes {
            if route.name == route_name {
                print_face(face);
                println!("    interests {}/{}", face.in_interests, face.out_interests);
                println!("    bytes     {}/{}", face.in_bytes, face.out_bytes);
                println!("    data      {}/{}", face.in_data, face.out_data);
                println!("    nacks     {}/{}", face.in_nacks, face.out_nacks);
                break;
            }
        }
    }
    Ok(())
}

fn print_json(input: &Input, json: &str) -> std::io::Result<()> {
    if json.starts_with("ERROR") {
        eprintln!("Agent error: {}", json);
    } else {
        match input.command {
            Command::Status => {
                print_status(json)?;
            }
            Command::Face => {
                if let Some(face_id) = input.face {
                    print_stats(face_id, json)?
                } else {
                    eprintln!("Error, pier-stats requires a face id (second argument)");
                }
            }
            Command::Piers => {
                let piers: Vec<Pier> = serde_json::from_str(&json)?;
                for pier in piers.iter() {
                    if pier.id == 0 {
                        println!(
                            "{}: {} LOCAL {}:{}",
                            pier.id, pier.prefix, pier.ip, pier.port
                        );
                    } else {
                        println!(
                            "{}: {} ({}) {}:{}",
                            pier.id, pier.prefix, pier.face_id, pier.ip, pier.port
                        );
                    }
                }
            }
            Command::QueryRoute => {
                if let Some(route) = &input.route {
                    print_stats_by_route(route, json)?;
                }
            }
            Command::Help => {}
        }
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
                .help("List the status of a pier (by id from --piers).")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("face")
                .long("face")
                .help("Detailed stats for a pier's face id.")
                .takes_value(true)
                .multiple(true)
                .number_of_values(2),
        )
        .arg(
            Arg::with_name("raw")
                .long("raw")
                .help("Sends a raw string to the agent and prints the resulting error or json.")
                .takes_value(true)
                .number_of_values(1),
        )
        .get_matches();

    let socket_name = matches.value_of("socket").unwrap_or("/tmp/ah");
    let mut stream = UnixStream::connect(socket_name)?;
    stream.set_nonblocking(true)?;

    let mut repl = true;
    if matches.is_present("piers") {
        writeln!(stream, "piers")?;
        let json = get_json(&mut stream)?;
        println!("PIERS:");
        print_json(
            &Input {
                command: Command::Piers,
                pier: None,
                face: None,
                route: None,
            },
            &json,
        )?;
        repl = false;
    }
    if matches.is_present("status") {
        let pier = matches
            .value_of("status")
            .unwrap_or("0")
            .parse::<u64>()
            .map_err(|e| {
                std::io::Error::new(
                    std::io::ErrorKind::Other,
                    format!("Pier not a number {}", e),
                )
            })?;
        writeln!(stream, "status {}", pier)?;
        let json = get_json(&mut stream)?;
        println!("PIER-STATUS pier {}:", pier);
        print_json(
            &Input {
                command: Command::Status,
                pier: None,
                face: None,
                route: None,
            },
            &json,
        )?;
        repl = false;
    }
    if matches.is_present("face") {
        if let Some(mut vals) = matches.values_of("face") {
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
            writeln!(stream, "status {}", pier)?;
            println!("PIER-STATS pier {} face {}:", pier, face);
            let json = get_json(&mut stream)?;
            print_json(
                &Input {
                    command: Command::Face,
                    pier: Some(pier),
                    face: Some(face),
                    route: None,
                },
                &json,
            )?;
        }
        repl = false;
    }
    if matches.is_present("raw") {
        if let Some(command) = matches.value_of("raw") {
            writeln!(stream, "{}", command)?;
            println!("{}:", command);
            let json = get_json(&mut stream)?;
            println!("{}", json);
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
                let parsed = parse_input(input);
                match parsed {
                    Ok((rest, inval)) => {
                        if !rest.is_empty() {
                            // Leftover input means the command was bad.
                            eprintln!("Parse Error on input: {}, unexpected {}", input, rest);
                        } else {
                            let mut get_print = true;
                            match inval.command {
                                Command::Status => {
                                    writeln!(stream, "status {}", inval.pier.unwrap())?
                                }
                                Command::Face => {
                                    writeln!(stream, "status {}", inval.pier.unwrap())?
                                }
                                Command::Piers => writeln!(stream, "piers")?,
                                Command::QueryRoute => {
                                    get_print = false;
                                    writeln!(stream, "piers")?;
                                    let json = get_json(&mut stream)?;
                                    let piers: Vec<Pier> = serde_json::from_str(&json)?;
                                    for pier in piers.iter() {
                                        if pier.id == 0 {
                                            println!(
                                                "pier: {} (LOCAL): {}@{}",
                                                pier.id, pier.prefix, pier.ip
                                            );
                                        } else {
                                            println!(
                                                "pier: {}: {}@{}",
                                                pier.id, pier.prefix, pier.ip
                                            );
                                        }
                                        writeln!(stream, "status {}", pier.id)?;
                                        let json = get_json(&mut stream)?;
                                        print_json(&inval, &json)?;
                                        println!();
                                    }
                                }
                                Command::Help => {
                                    get_print = false;
                                    println!();
                                    println!("Available Commands");
                                    println!("status [pier]      : status of pier (default to 0- i.e. local)");
                                    println!("face [pier] face_id: face info for pier's face_id (default to 0- i.e. local)");
                                    println!("piers              : list known piers");
                                    println!("route name         : find and print info on the route name");
                                    println!("help               : this");
                                    println!();
                                }
                            }
                            if get_print {
                                let json = get_json(&mut stream)?;
                                print_json(&inval, &json)?;
                            }
                        }
                    }
                    Err(err) => eprintln!("Parse Error on input: {}, {}", input, err),
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
