extern crate nom;

use self::nom::character::complete::digit1;
use nom::{
    branch::alt,
    bytes::complete::tag_no_case,
    error::{context, VerboseError},
    IResult,
};

#[derive(Debug, PartialEq, Eq)]
pub enum Command {
    Status,
    PierStatus,
    Stats,
    PierStats,
    Piers,
}

impl From<&str> for Command {
    fn from(i: &str) -> Self {
        match i.to_lowercase().as_str() {
            "status" => Command::Status,
            "pier-status" => Command::PierStatus,
            "stats" => Command::Stats,
            "pier-stats" => Command::PierStats,
            "piers" => Command::Piers,
            _ => unimplemented!("command not supported"),
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct Input {
    pub command: Command,
    pub pier: Option<u64>,
    pub face: Option<u64>,
}

type Res<T, U> = IResult<T, U, VerboseError<T>>;

fn command(input: &str) -> Res<&str, Command> {
    context(
        "command",
        alt((
            tag_no_case("status"),
            tag_no_case("pier-status"),
            tag_no_case("stats"),
            tag_no_case("pier-stats"),
            tag_no_case("piers"),
        )),
    )(input)
    .map(|(next_input, res)| (next_input, res.into()))
}

fn number(input: &str) -> Res<&str, u64> {
    context("number", digit1)(input).map(|(next_input, res)| {
        let num: u64 = res.parse().unwrap_or(0);
        (next_input, num)
    })
}

pub fn parse_input(input: &str) -> Res<&str, Input> {
    let (input, command) = command(input)?;
    match command {
        Command::Status => Ok((
            input,
            Input {
                command,
                pier: None,
                face: None,
            },
        )),
        Command::PierStatus => {
            let (input, pier) = number(input.trim())?;
            Ok((
                input,
                Input {
                    command,
                    pier: Some(pier),
                    face: None,
                },
            ))
        }
        Command::Stats => {
            let (input, face) = number(input.trim())?;
            Ok((
                input,
                Input {
                    command,
                    pier: None,
                    face: Some(face),
                },
            ))
        }
        Command::PierStats => {
            let (input, pier) = number(input.trim())?;
            let (input, face) = number(input.trim())?;
            Ok((
                input,
                Input {
                    command,
                    pier: Some(pier),
                    face: Some(face),
                },
            ))
        }
        Command::Piers => Ok((
            input,
            Input {
                command,
                pier: None,
                face: None,
            },
        )),
    }
    /*    context("input", tuple((command, opt(tag(" ")), opt(number), opt(tag(" ")), opt(number))))(input).map(
        |(next_input, (command, _, pier, _, face))| {
            (
                next_input,
                Input {
                    command,
                    pier,
                    face,
                },
            )
        },
    )*/
}

#[cfg(test)]
mod tests {
    use super::nom::error::{ErrorKind, VerboseErrorKind};
    use super::*;

    #[test]
    fn test_command() {
        assert_eq!(command("status"), Ok(("", Command::Status)));
        assert_eq!(command("pier-status 0"), Ok((" 0", Command::PierStatus)));
        assert_eq!(command("stats 250"), Ok((" 250", Command::Stats)));
        assert_eq!(
            command("pier-stats 0 250"),
            Ok((" 0 250", Command::PierStats))
        );
        assert_eq!(command("piers"), Ok(("", Command::Piers)));
        assert_eq!(
            command("nothing"),
            Err(nom::Err::Error(VerboseError {
                errors: vec![
                    ("nothing", VerboseErrorKind::Nom(ErrorKind::Tag)),
                    ("nothing", VerboseErrorKind::Nom(ErrorKind::Alt)),
                    ("nothing", VerboseErrorKind::Context("command")),
                ]
            }))
        );
    }

    #[test]
    fn test_number() {
        assert_eq!(number("0"), Ok(("", 0)));
        assert_eq!(number("10"), Ok(("", 10)));
        assert_eq!(number("10000"), Ok(("", 10_000)));
        assert_eq!(
            number("nothing"),
            Err(nom::Err::Error(VerboseError {
                errors: vec![
                    ("nothing", VerboseErrorKind::Nom(ErrorKind::Digit)),
                    ("nothing", VerboseErrorKind::Context("number")),
                ]
            }))
        );
        assert_eq!(
            number("-10"),
            Err(nom::Err::Error(VerboseError {
                //Err(VerboseError {
                errors: vec![
                    ("-10", VerboseErrorKind::Nom(ErrorKind::Digit)),
                    ("-10", VerboseErrorKind::Context("number")),
                ]
            }))
        );
        /*assert_eq!(
            number("10.5"),
            Err(nom::Err::Error(VerboseError {
                errors: vec![
                    ("10.5", VerboseErrorKind::Nom(ErrorKind::Digit)),
                    ("10.5", VerboseErrorKind::Context("number")),
                ]
            }))
        );*/
    }

    #[test]
    fn test_input() {
        assert_eq!(
            parse_input("status"),
            Ok((
                "",
                Input {
                    command: Command::Status,
                    pier: None,
                    face: None
                }
            ))
        );
        assert_eq!(
            parse_input("piers"),
            Ok((
                "",
                Input {
                    command: Command::Piers,
                    pier: None,
                    face: None
                }
            ))
        );
        assert_eq!(
            parse_input("pier-status 0"),
            Ok((
                "",
                Input {
                    command: Command::PierStatus,
                    pier: Some(0),
                    face: None
                }
            ))
        );
        assert_eq!(
            parse_input("stats 250"),
            Ok((
                "",
                Input {
                    command: Command::Stats,
                    pier: None,
                    face: Some(250)
                }
            ))
        );
        assert_eq!(
            parse_input("pier-stats 0 250"),
            Ok((
                "",
                Input {
                    command: Command::PierStats,
                    pier: Some(0),
                    face: Some(250)
                }
            ))
        );
        assert_eq!(
            parse_input("nothing"),
            Err(nom::Err::Error(VerboseError {
                errors: vec![
                    ("nothing", VerboseErrorKind::Nom(ErrorKind::Tag)),
                    ("nothing", VerboseErrorKind::Nom(ErrorKind::Alt)),
                    ("nothing", VerboseErrorKind::Context("command")),
                    //("nothing", VerboseErrorKind::Context("input")),
                ]
            }))
        );
        assert_eq!(
            parse_input("pier-status"),
            Err(nom::Err::Error(VerboseError {
                errors: vec![
                    ("", VerboseErrorKind::Nom(ErrorKind::Digit)),
                    ("", VerboseErrorKind::Context("number")),
                    //("nothing", VerboseErrorKind::Context("input")),
                ]
            }))
        );
        assert_eq!(
            parse_input("pier-stats 0"),
            Err(nom::Err::Error(VerboseError {
                errors: vec![
                    ("", VerboseErrorKind::Nom(ErrorKind::Digit)),
                    ("", VerboseErrorKind::Context("number")),
                    //("nothing", VerboseErrorKind::Context("input")),
                ]
            }))
        );
    }
}
