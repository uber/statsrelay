use bytes::Bytes;
use criterion::{black_box, criterion_group, criterion_main, Criterion};

fn parse(line: &Bytes) -> Option<statsrelay::statsd::StatsdPDU> {
    statsrelay::statsd::StatsdPDU::new(line.clone())
}

fn criterion_benchmark(c: &mut Criterion) {
    let by = Bytes::from_static(
        b"hello_world.worldworld_i_am_a_pumpkin:3|c|@1.0|#tags:tags,tags:tags,tags:tags,tags:tags",
    );
    c.bench_function("statsd pdu parsing", |b| b.iter(|| parse(black_box(&by))));
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
