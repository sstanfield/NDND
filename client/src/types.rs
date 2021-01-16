use std::fmt;

use serde::{Deserialize, Serialize};

fn zero() -> u64 {
    0
}

#[derive(Serialize, Deserialize, Debug)]
pub enum LinkType {
    #[serde(rename = "none")]
    None,
    #[serde(rename = "point-to-point")]
    PointToPoint,
    #[serde(rename = "multi-access")]
    MultiAccess,
    #[serde(rename = "ad-hoc")]
    AdHoc,
}

impl fmt::Display for LinkType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Self::None => write!(f, "none"),
            Self::PointToPoint => write!(f, "point-to-point"),
            Self::MultiAccess => write!(f, "multi-access"),
            Self::AdHoc => write!(f, "ad-hoc"),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum FaceScope {
    #[serde(rename = "none")]
    None,
    #[serde(rename = "local")]
    Local,
    #[serde(rename = "non-local")]
    NonLocal,
}

impl fmt::Display for FaceScope {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Self::None => write!(f, "none"),
            Self::Local => write!(f, "local"),
            Self::NonLocal => write!(f, "non-local"),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum FacePersistency {
    #[serde(rename = "none")]
    None,
    #[serde(rename = "persistent")]
    Persistent,
    #[serde(rename = "on-demand")]
    OnDemand,
    #[serde(rename = "permanent")]
    Permanent,
}

impl fmt::Display for FacePersistency {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Self::None => write!(f, "none"),
            Self::Persistent => write!(f, "persistent"),
            Self::OnDemand => write!(f, "on-demand"),
            Self::Permanent => write!(f, "permanent"),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum RouteOrigin {
    #[serde(rename = "none")]
    None,
    #[serde(rename = "app")]
    App,
    #[serde(rename = "auto-reg")]
    AutoReg,
    #[serde(rename = "client")]
    Client,
    #[serde(rename = "auto-conf")]
    AutoConf,
    #[serde(rename = "nlsr")]
    Nlsr,
    #[serde(rename = "prefix-ann")]
    PrefixAnn,
    #[serde(rename = "static")]
    Static,
}

impl fmt::Display for RouteOrigin {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Self::None => write!(f, "none"),
            Self::App => write!(f, "app"),
            Self::AutoReg => write!(f, "auto-reg"),
            Self::Client => write!(f, "client"),
            Self::AutoConf => write!(f, "auto-conf"),
            Self::Nlsr => write!(f, "nlsr"),
            Self::PrefixAnn => write!(f, "prefix-ann"),
            Self::Static => write!(f, "static"),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Route {
    pub name: String,
    pub origin: RouteOrigin,
    pub cost: u64,
    #[serde(default = "zero")]
    pub expiration_period_ms: u64,
    pub flags: u64,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Face {
    pub id: u64,
    pub remote_uri: String,
    pub local_uri: String,
    pub link_type: LinkType,
    pub face_scope: FaceScope,
    pub face_persistency: FacePersistency,
    pub flags: u64,
    pub in_interests: u64,
    pub out_interests: u64,
    pub in_bytes: u64,
    pub out_bytes: u64,
    pub in_data: u64,
    pub out_data: u64,
    pub in_nacks: u64,
    pub out_nacks: u64,
    pub mtu: u64,
    pub default_congestion_threshold: u64,
    pub default_base_congestion_marking_interval_ns: u64,
    #[serde(default = "zero")]
    pub expiration_period_ms: u64,
    pub routes: Vec<Route>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Pier {
    pub id: u64,
    #[serde(rename = "faceId")]
    pub face_id: u64,
    pub prefix: String,
    pub ip: String,
    pub port: u64,
}
