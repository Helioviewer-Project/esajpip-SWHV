// Same determinant and truncation cases as repro.c.
mod min;
mod minDef;
use asn1rust::*;
use minDef::*;

macro_rules! check {
    ($ty:ident, $decode:path, $min:expr, $max:expr, $count:expr,
     $data:ident, $size:ident, $length:ident, $passed:ident) => {{
        let mut value = $ty::default();
        let mut error = 0;
        let expected_bytes = $data;
        let mut stream = BitStream::attach_buffer_no_zero(&mut $data[..$size]);
        let decoded = $decode(&mut value, &mut stream, &mut error);
        let expected = $length >= $min && $length <= $max && $size >= 1 + $length;
        let ok = if expected {
            decoded && error == 0 && ($count)(&value) == $length &&
                value.data.arr[..$length] == expected_bytes[1..1 + $length]
        } else { !decoded && error != 0 };
        if !ok {
            eprintln!("{}: len={} size={} decoded={} error={}",
                      stringify!($ty), $length, $size, decoded, error);
            $passed = false;
        }
    }};
}

fn main() {
    let mut passed = true;
    for length in [0usize, 1, 2, 8, 9, 255] {
        for size in 0..=9usize {
            let mut data = [length as u8, 1, 2, 3, 4, 5, 6, 7, 8];
            check!(Bounded, min::Bounded_ACN_Decode, 1, 8,
                   |v: &Bounded| v.data.n_count as usize, data, size, length, passed);
            check!(ZeroMin, min::ZeroMin_ACN_Decode, 0, 8,
                   |v: &ZeroMin| v.data.n_count as usize, data, size, length, passed);
            check!(Fixed, min::Fixed_ACN_Decode, 2, 2,
                   |_: &Fixed| 2usize, data, size, length, passed);
        }
    }
    std::process::exit(if passed { 0 } else { 1 });
}
