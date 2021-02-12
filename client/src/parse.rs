extern crate nom;

use self::nom::character::complete::digit1;
use self::nom::error::ErrorKind;
use self::nom::{AsChar, InputTakeAtPosition};
use nom::{
    branch::alt,
    bytes::complete::tag_no_case,
    error::{context, VerboseError},
    IResult,
};

#[derive(Debug, PartialEq, Eq)]
pub enum Command {
    Status,
    Face,
    Piers,
    QueryRoute,
}

impl From<&str> for Command {
    fn from(i: &str) -> Self {
        match i.to_lowercase().as_str() {
            "status" => Command::Status,
            "face" => Command::Face,
            "piers" => Command::Piers,
            "route" => Command::QueryRoute,
            _ => unimplemented!("command not supported"),
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct Input {
    pub command: Command,
    pub pier: Option<u64>,
    pub face: Option<u64>,
    pub route: Option<String>,
}

type Res<T, U> = IResult<T, U, VerboseError<T>>;

fn command(input: &str) -> Res<&str, Command> {
    context(
        "command",
        alt((
            tag_no_case("status"),
            tag_no_case("face"),
            tag_no_case("piers"),
            tag_no_case("route"),
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

fn routechars1<T>(i: T) -> Res<T, T>
where
    T: InputTakeAtPosition,
    <T as InputTakeAtPosition>::Item: AsChar,
{
    i.split_at_position1_complete(
        |item| {
            let char_item = item.as_char();
            char_item != '-' && char_item != '/' && char_item != '\\' && !char_item.is_alphanum()
        },
        ErrorKind::AlphaNumeric,
    )
}

fn route(input: &str) -> Res<&str, String> {
    context("route", routechars1)(input).map(|(next_input, res)| (next_input, res.to_string()))
}

pub fn parse_input(input: &str) -> Res<&str, Input> {
    let (input, command) = command(input)?;
    match command {
        Command::Status => {
            let (input, pier) = if input.trim().is_empty() {
                (input, 0)
            } else {
                number(input.trim())?
            };
            Ok((
                input,
                Input {
                    command,
                    pier: Some(pier),
                    face: None,
                    route: None,
                },
            ))
        }
        Command::Face => {
            let (input, mut pier) = number(input.trim())?;
            let (input, face) = if input.trim().is_empty() {
                let tp = pier;
                pier = 0;
                (input, tp)
            } else {
                number(input.trim())?
            };
            Ok((
                input,
                Input {
                    command,
                    pier: Some(pier),
                    face: Some(face),
                    route: None,
                },
            ))
        }
        Command::Piers => Ok((
            input,
            Input {
                command,
                pier: None,
                face: None,
                route: None,
            },
        )),
        Command::QueryRoute => {
            let (input, route) = route(input.trim())?;
            Ok((
                input,
                Input {
                    command,
                    pier: None,
                    face: None,
                    route: Some(route),
                },
            ))
        }
    }
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
