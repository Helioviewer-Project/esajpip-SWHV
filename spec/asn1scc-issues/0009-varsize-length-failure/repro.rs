// repro.c in Rust: decodes Mask and Bits from 0xF0 (length 15) and from no
// input. Exit status 0 when every decode fails with a nonzero error code.
mod min;
mod minDef;
use asn1rust::*;
use minDef::*;

fn main() {
    let mut passed = true;
    for size in [1usize, 0] {
        for (name, which) in [("Mask uPER", 0), ("Mask ACN", 1), ("Bits uPER", 2), ("Bits ACN", 3)] {
            let mut data = [0xF0u8];
            let mut stream = BitStream::attach_buffer_no_zero(&mut data[..size]);
            let mut error = 0;
            let mut mask = Mask::default();
            let mut bits = Bits::default();
            let decoded = match which {
                0 => min::Mask_Decode(&mut mask, &mut stream, &mut error),
                1 => min::Mask_ACN_Decode(&mut mask, &mut stream, &mut error),
                2 => min::Bits_Decode(&mut bits, &mut stream, &mut error),
                _ => min::Bits_ACN_Decode(&mut bits, &mut stream, &mut error),
            };
            println!("{}, {} bytes: decoded={} error={}", name, size, decoded, error);
            passed &= !decoded && error != 0;
        }
    }
    std::process::exit(if passed { 0 } else { 1 });
}
