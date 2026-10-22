#include "common.hpp"
#include "dma_fixture.hpp"
using namespace campaign_dma;
CASE(71,"NXR-135","DMA rejects null CPU staging"){Fixture f;auto b=f.buffer();b.cpu=nullptr;assert(!f.channel.initialize(backend(f.engine),b,Direction::transmit));}
CASE(72,"NXR-135","DMA rejects empty staging allocation"){Fixture f;auto b=f.buffer();b.capacity=0;assert(!f.channel.initialize(backend(f.engine),b,Direction::transmit));}
CASE(73,"NXR-135","DMA rejects zero alignment"){Fixture f;auto b=f.buffer();b.alignment=0;assert(!f.channel.initialize(backend(f.engine),b,Direction::transmit));}
CASE(74,"NXR-135","DMA rejects non-power-of-two alignment"){Fixture f;auto b=f.buffer();b.alignment=3;assert(!f.channel.initialize(backend(f.engine),b,Direction::transmit));}
CASE(75,"NXR-135","DMA rejects partial cache-line capacity"){Fixture f;auto b=f.buffer();b.capacity=127;assert(!f.channel.initialize(backend(f.engine),b,Direction::transmit));}
CASE(76,"NXR-135","DMA backend window denial preserves uninitialized state"){Fixture f;f.engine.accepts=false;assert(!f.bind());Snapshot s;assert(f.channel.snapshot(s)==Status::ok&&s.state==State::uninitialized);}
CASE(77,"NXR-133","DMA missing stop proof callback rejected"){Fixture f;auto b=backend(f.engine);b.stop=nullptr;assert(!f.channel.initialize(b,f.buffer(),Direction::transmit));}
CASE(78,"NXR-133","DMA invalid direction rejected"){Fixture f;assert(!f.channel.initialize(backend(f.engine),f.buffer(),static_cast<Direction>(99)));}
CASE(79,"NXR-135","DMA rejects unaligned bus address"){Fixture f;auto b=f.buffer();b.bus++;assert(!f.channel.initialize(backend(f.engine),b,Direction::transmit));}
CASE(80,"NXR-134","DMA staging cannot overlap channel metadata"){Fixture f;auto b=f.buffer();b.cpu=reinterpret_cast<uint8_t*>(&f.channel);b.alignment=1;assert(!f.channel.initialize(backend(f.engine),b,Direction::transmit));}
