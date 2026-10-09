//! Tuning::default(), every float and bool field by its Rust name, from the Debug output, so the
//! C++ port can be checked field by field without a hand-written list on this side.
use faith_move::tuning::Tuning;

/// A level of nesting in the pretty Debug output, and how it lists values.
#[derive(Clone, PartialEq)]
enum Kind {
    /// A struct (or a skipped tuple): `name: value` lines.
    Fields,
    /// An array: bare values, numbered.
    Array(usize),
    /// `Some(` around one bare value.
    Some,
}

/// Walks `{:#?}` output: `name: value,` becomes `name value`; nested structs prefix their fields
/// (`loco.max_base`); array elements are numbered (`takedown_clips[0]`); `Some(x)` is x; bools
/// are 0/1; `None` and the accel curve's points are skipped.
pub fn txt() -> String {
    let debug = format!("{:#?}", Tuning::default());
    let mut out = String::new();
    let mut stack: Vec<(String, Kind)> = vec![];
    let prefix = |st: &[(String, Kind)]| -> String { st.iter().map(|(n, _)| format!("{n}.")).collect() };
    for raw in debug.lines().skip(1) {
        let line = raw.trim().trim_end_matches(',');
        if line == "}" || line == "]" || line == ")" {
            stack.pop();
            continue;
        }
        let in_curve = stack.iter().any(|(n, _)| n == "accel_curve");
        if let Some((name, value)) = line.split_once(": ") {
            let kind = match value {
                "[" => Some(Kind::Array(0)),
                "Some(" => Some(Kind::Some),
                v if v.ends_with('{') || v.ends_with('(') => Some(Kind::Fields),
                _ => None,
            };
            if let Some(k) = kind {
                stack.push((name.to_string(), k));
                continue;
            }
            if in_curve {
                continue;
            }
            let v = match value { "true" => "1", "false" => "0", other => other };
            if v.parse::<f32>().is_ok() {
                out += &format!("{}{name} {v}\n", prefix(&stack));
            }
        } else if line.ends_with('[') || line.ends_with('(') || line.ends_with('{') {
            stack.push((String::new(), Kind::Fields));
        } else if !in_curve && line.parse::<f32>().is_ok() {
            let n = stack.len();
            match stack.last().cloned() {
                Some((name, Kind::Array(i))) => {
                    stack[n - 1].1 = Kind::Array(i + 1);
                    out += &format!("{}{name}[{i}] {line}\n", prefix(&stack[..n - 1]));
                }
                Some((name, Kind::Some)) => out += &format!("{}{name} {line}\n", prefix(&stack[..n - 1])),
                _ => {}
            }
        }
    }
    out
}
