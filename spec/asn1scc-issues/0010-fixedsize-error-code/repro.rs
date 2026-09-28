// The same truncation and complete-input checks as repro.c.
mod min;
mod minDef;
use asn1rust::*;
use minDef::*;

fn main() {
    let mut passed = true;
    for size in 0..=16usize {
        for which in 0..4 {
            let mut data = [0u8; 16];
            for (i, byte) in data.iter_mut().enumerate() { *byte = i as u8; }
            let expected = data;
            let mut stream = BitStream::attach_buffer_no_zero(&mut data[..size]);
            let mut error = 0;
            let mut uuid = UuidId::default();
            let mut envelope = Envelope::default();
            let decoded = match which {
                0 => min::UuidId_Decode(&mut uuid, &mut stream, &mut error),
                1 => min::UuidId_ACN_Decode(&mut uuid, &mut stream, &mut error),
                2 => min::Envelope_Decode(&mut envelope, &mut stream, &mut error),
                _ => min::Envelope_ACN_Decode(&mut envelope, &mut stream, &mut error),
            };
            let bytes = if which < 2 { &uuid.arr } else { &envelope.id.arr };
            let ok = if size < 16 { !decoded && error != 0 }
                     else { decoded && error == 0 && bytes == &expected };
            if !ok {
                eprintln!("decoder {}, {} bytes: decoded={} error={}", which, size, decoded, error);
                passed = false;
            }
        }
    }
    std::process::exit(if passed { 0 } else { 1 });
}
