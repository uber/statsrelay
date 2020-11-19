use murmur3;
use std::io::Cursor;

use crate::statsd::StatsdPDU;

// HASHLIB_SEED same as the legacy statsrelay code base
const HASHLIB_SEED: u32 = 0xaccd3d34;

pub fn statsrelay_compat_hash(pdu: &StatsdPDU) -> u32 {
    murmur3::murmur3_32(&mut Cursor::new(pdu.name()), HASHLIB_SEED).unwrap_or(0)
}

pub struct Ring<C: Send + Sync + 'static> {
    members: Vec<C>,
}

impl<C: Send + Sync + 'static> Ring<C> {
    pub fn new() -> Self {
        Ring {
            members: Vec::new(),
        }
    }

    pub fn push(&mut self, c: C) {
        self.members.push(c);
    }

    pub fn len(&self) -> usize {
        self.members.len()
    }

    pub fn pick_from(&self, code: u32) -> &C {
        let l = self.members.len();
        self.members.get(code as usize % l).unwrap()
    }

    pub fn act_on<F>(&mut self, code: u32, mut f: F)
    where
        F: FnMut(&mut C),
    {
        let len = self.members.len();
        let c = &mut self.members[code as usize % len];
        f(c);
    }

    pub fn swap(&mut self, other: Ring<C>) {
        self.members = other.members;
    }
}

#[cfg(test)]
pub mod test {
    use super::*;
    use bytes::Bytes;

    #[test]
    fn test_swap() {
        let mut ring = Ring::new();
        ring.push(0);
        ring.push(1);
        assert_eq!(ring.len(), 2);
        let mut ring2 = Ring::new();
        ring2.push(2);
        ring2.push(3);
        ring2.push(4);
        assert_eq!(ring2.len(), 3);
        ring.swap(ring2);
        assert_eq!(ring.len(), 3);

    }
    #[test]
    fn test_hash() {
        let mut ring = Ring::new();
        ring.push(0);
        ring.push(1);
        ring.push(2);
        ring.push(3);

        assert_eq!(
            *ring.pick_from(statsrelay_compat_hash(
                &StatsdPDU::new(Bytes::copy_from_slice(b"apple:1|c")).unwrap()
            )),
            2
        );
        assert_eq!(
            *ring.pick_from(statsrelay_compat_hash(
                &StatsdPDU::new(Bytes::copy_from_slice(b"banana:1|c")).unwrap()
            )),
            3
        );
        assert_eq!(
            *ring.pick_from(statsrelay_compat_hash(
                &StatsdPDU::new(Bytes::copy_from_slice(b"orange:1|c")).unwrap()
            )),
            0
        );
        assert_eq!(
            *ring.pick_from(statsrelay_compat_hash(
                &StatsdPDU::new(Bytes::copy_from_slice(b"lemon:1|c")).unwrap()
            )),
            1
        );
    }
}
