import test from 'node:test';
import assert from 'node:assert/strict';
import { inspectRom, validateSmb3Rom } from '../tools/rom.mjs';

function fixture(trainer = false) {
  const data = Buffer.alloc(16 + (trainer ? 512 : 0) + 32768 + 8192);
  Buffer.from([0x4e, 0x45, 0x53, 0x1a, 2, 1, 0x40 | (trainer ? 4 : 0)]).copy(data);
  const end = 16 + (trainer ? 512 : 0) + 32768;
  for (let offset = end - 6; offset < end; offset += 2) data.writeUInt16LE(0xe000 + offset % 256, offset);
  return data;
}

test('separa bancos MMC3 e lê vetores do banco fixo', () => {
  const report = inspectRom(fixture());
  assert.equal(report.mapper, 4);
  assert.equal(report.banks.length, 4);
  assert.equal(report.banks[3].fileOffset, 16 + 24576);
  assert.equal(report.vectors.reset, 0xe00c);
  assert.equal(report.chrOffset, 32784);
});
test('trainer altera offsets sem alterar PRG', () => {
  const report = inspectRom(fixture(true));
  assert.equal(report.prgOffset, 528);
  assert.equal(report.chrOffset, 33296);
});
test('rejeita arquivo truncado, lixo final, mapper errado e NES 2.0', () => {
  assert.throws(() => inspectRom(Buffer.alloc(16)), /inválido/);
  assert.throws(() => inspectRom(fixture().subarray(0, -1)), /Tamanho/);
  assert.throws(() => inspectRom(Buffer.concat([fixture(), Buffer.alloc(1)])), /Tamanho/);
  const wrongMapper = fixture(); wrongMapper[6] = 0;
  assert.throws(() => inspectRom(wrongMapper), /Mapper/);
  const nes2 = fixture(); nes2[7] = 8; nes2[9] = 1;
  assert.throws(() => inspectRom(nes2), /NES 2.0/);
});
test('aceita NES 2.0 MMC3 sem extensões', () => {
  const nes2 = fixture(); nes2[7] = 8;
  assert.equal(inspectRom(nes2).format, 'NES 2.0');
});

test('o mesmo PRG/CHR em cabeçalhos iNES e NES 2.0 tem os mesmos hashes', () => {
  const ines = fixture();
  const nes2 = fixture();
  nes2[7] = 8;
  const a = inspectRom(ines);
  const b = inspectRom(nes2);
  assert.equal(a.format, 'iNES');
  assert.equal(b.format, 'NES 2.0');
  assert.equal(a.prgSha256, b.prgSha256);
  assert.equal(a.chrSha256, b.chrSha256);
});

test('validateSmb3Rom rejeita um payload que não é a revisão suportada', () => {
  assert.throws(() => validateSmb3Rom(fixture()), /Super Mario Bros|revisão suportada|hash/);
});
