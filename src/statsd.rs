use bytes::BufMut;
use bytes::Bytes;
use memchr::memchr;

/// A StatsdPDU is an incoming protocol unit for statsd messages, commonly a
/// single datagram or a line-delimitated message. This PDU type owns an
/// incoming message and can offer references to protocol fields. It only
/// performs limited parsing of the protocol unit.
#[derive(Debug, Clone)]
pub struct StatsdPDU {
    underlying: Bytes,
    value_index: usize,
    type_index: usize,
    type_index_end: usize,
    sample_rate_index: Option<(usize, usize)>,
    tags_index: Option<(usize, usize)>,
}

impl StatsdPDU {
    pub fn name(&self) -> &[u8] {
        &self.underlying[0..self.value_index - 1]
    }

    pub fn value(&self) -> &[u8] {
        &self.underlying[self.value_index..self.type_index - 1]
    }

    pub fn pdu_type(&self) -> &[u8] {
        &self.underlying[self.type_index..self.type_index_end]
    }

    pub fn tags(&self) -> Option<&[u8]> {
        self.tags_index.map(|v| &self.underlying[v.0..v.1])
    }

    pub fn sample_rate(&self) -> Option<&[u8]> {
        self.sample_rate_index.map(|v| &self.underlying[v.0..v.1])
    }

    pub fn len(&self) -> usize {
        self.underlying.len()
    }

    pub fn as_ref(&self) -> &[u8] {
        self.underlying.as_ref()
    }

    ///
    /// Return a clone of the PDU with a prefix and suffix attached to the statsd name
    ///
    pub fn with_prefix_suffix(&self, prefix: &[u8], suffix: &[u8]) -> Self {
        let offset = suffix.len() + prefix.len();

        let mut buf = bytes::BytesMut::with_capacity(self.len() + offset);
        buf.put(prefix);
        buf.put(self.name());
        buf.put(suffix);
        buf.put(self.underlying[self.value_index - 1..].as_ref());

        StatsdPDU {
            underlying: buf.freeze(),
            value_index: self.value_index + offset,
            type_index: self.type_index + offset,
            type_index_end: self.type_index_end + offset,
            sample_rate_index: self
                .sample_rate_index
                .map(|(b, e)| (b + offset, e + offset)),
            tags_index: self.tags_index.map(|(b, e)| (b + offset, e + offset)),
        }
    }

    /// Parse an incoming single protocol unit and capture internal field
    /// offsets for the positions and lengths of various protocol fields for
    /// later access. No parsing or validation of values is done, so at a low
    /// level this can be used to pass through unknown types and protocols.
    pub fn new(line: Bytes) -> Option<Self> {
        let length = line.len();
        let mut value_index: usize = 0;
        // To support inner ':' symbols in a metric name (more common than you
        // think) we'll first find the index of the first type separator, and
        // then do a walk to find the last ':' symbol before that.
        let type_index = memchr('|' as u8, &line)? + 1;

        loop {
            let value_check_index = memchr(':' as u8, &line[value_index..type_index]);
            match (value_check_index, value_index) {
                (None, x) if x <= 0 => return None,
                (None, _) => break,
                _ => (),
            }
            value_index = value_check_index.unwrap() + value_index + 1;
        }
        let mut type_index_end = length;
        let mut sample_rate_index: Option<(usize, usize)> = None;
        let mut tags_index: Option<(usize, usize)> = None;

        let mut scan_index = type_index;
        loop {
            let index = memchr('|' as u8, &line[scan_index..]).map(|v| v + scan_index);
            match index {
                None => break,
                Some(x) if x + 2 >= length => break,
                Some(x) if x < type_index_end => type_index_end = x,
                _ => (),
            }
            match line[index.unwrap() + 1] {
                b'@' => {
                    if sample_rate_index.is_some() {
                        return None;
                    }
                    sample_rate_index = index.map(|v| (v + 2, length));
                    tags_index = tags_index.map(|(v, _l)| (v, index.unwrap()));
                }
                b'#' => {
                    if tags_index.is_some() {
                        return None;
                    }
                    tags_index = index.map(|v| (v + 2, length));
                    sample_rate_index = sample_rate_index.map(|(v, _l)| (v, index.unwrap()));
                }
                _ => return None,
            }
            scan_index = index.unwrap() + 1;
        }
        Some(StatsdPDU {
            underlying: line,
            value_index,
            type_index,
            type_index_end,
            sample_rate_index: sample_rate_index,
            tags_index: tags_index,
        })
    }
}

#[cfg(test)]
pub mod atest {
    use super::*;
    use anyhow::anyhow;

    #[test]
    fn parse_pdus() -> anyhow::Result<()> {
        let valid: Vec<Vec<u8>> = vec![
            b"foo.bar:3|c".to_vec(),
            b"car:bar:3|c".to_vec(),
            b"hello.bar:4.0|ms|#tags".to_vec(),
            b"hello.bar:4.0|ms|@1.0|#tags".to_vec(),
        ];
        for buf in valid {
            println!("{}", String::from_utf8(buf.clone())?);
            StatsdPDU::new(buf.into()).ok_or(anyhow!("no pdu"))?;
        }
        Ok(())
    }

    #[test]
    fn simple_pdu() {
        let pdu = StatsdPDU::new(Bytes::from_static(b"foo.car:bar:3.0|c")).unwrap();
        assert_eq!(pdu.name(), b"foo.car:bar");
        assert_eq!(pdu.value(), b"3.0");
        assert_eq!(pdu.pdu_type(), b"c")
    }

    #[test]
    fn tagged_pdu() {
        let pdu = StatsdPDU::new(Bytes::from_static(b"foo.bar:3|c|@1.0|#tags")).unwrap();
        assert_eq!(pdu.name(), b"foo.bar");
        assert_eq!(pdu.value(), b"3");
        assert_eq!(pdu.pdu_type(), b"c");
        assert_eq!(pdu.tags().unwrap(), b"tags");
        assert_eq!(pdu.sample_rate().unwrap(), b"1.0");
    }

    #[test]
    fn tagged_pdu_reverse() {
        let pdu = StatsdPDU::new(Bytes::from_static(b"foo.bar:3|c|#tags|@1.0")).unwrap();
        assert_eq!(pdu.name(), b"foo.bar");
        assert_eq!(pdu.value(), b"3");
        assert_eq!(pdu.pdu_type(), b"c");
        assert_eq!(pdu.tags().unwrap(), b"tags");
        assert_eq!(pdu.sample_rate().unwrap(), b"1.0");
    }

    #[test]
    fn prefix_suffix_test() {
        let opdu = StatsdPDU::new(Bytes::from_static(b"foo.bar:3|c|#tags|@1.0")).unwrap();
        let pdu = opdu.with_prefix_suffix(b"aa", b"bbb");
        assert_eq!(pdu.name(), b"aafoo.barbbb");
        assert_eq!(pdu.value(), b"3");
        assert_eq!(pdu.pdu_type(), b"c");
        assert_eq!(pdu.tags().unwrap(), b"tags");
        assert_eq!(pdu.sample_rate().unwrap(), b"1.0");
    }
}
