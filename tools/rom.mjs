// iNES / NES 2.0 ROM inspection for the build and its tests.
//
// The SMB3 project accepts both the goodNES dump and the No-Intro/Rev 1 dump.
// They carry the same PRG and CHR payload; only the 16-byte header differs.
// Comparing the payload hashes, instead of the whole file, accepts both without
// weakening the revision check: a different game or revision still fails.

import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

export const SMB3_PAYLOAD = Object.freeze({
  prgSha256: 'f4130ba655229d9999fa3b2c59984165810c9b9b125c43b1ff899872e7fedff4',
  chrSha256: '9a4b0b44031afd7d9574b23e4ef24daf1c06a140cfd19ad2db99bd6344780656',
  prgSize: 262144,
  chrSize: 131072,
});

const MAGIC = [0x4e, 0x45, 0x53, 0x1a];

function fail(message) {
  throw new Error(message);
}

function hash(bytes, offset, length) {
  return createHash('sha256').update(bytes.subarray(offset, offset + length)).digest('hex');
}

export function inspectRom(data) {
  if (!Buffer.isBuffer(data)) fail('ROM inválida: esperado um Buffer');
  if (data.length < 16) fail('Cabeçalho iNES inválido: arquivo menor que 16 bytes');
  for (let i = 0; i < MAGIC.length; i++) {
    if (data[i] !== MAGIC[i]) fail('Cabeçalho iNES inválido: assinatura NES ausente');
  }

  const flags6 = data[6];
  const flags7 = data[7];
  const flags8 = data[8];
  const flags9 = data[9];
  const nes2 = (flags7 & 0x0c) === 0x08;
  const format = nes2 ? 'NES 2.0' : 'iNES';

  // NES 2.0 console type: 0 = NES/Famicom. The project only supports that.
  if (nes2 && (flags9 & 0x03) !== 0) {
    fail(`NES 2.0 com console ${flags9 & 0x03} não suportado`);
  }

  const mapper = (flags6 >> 4) | (flags7 & 0xf0) | (nes2 ? (flags8 & 0x0f) << 8 : 0);
  if (mapper !== 4) fail(`Mapper ${mapper} não suportado (esperado 4, MMC3)`);

  const trainer = (flags6 & 0x04) ? 512 : 0;
  const prgOffset = 16 + trainer;
  const prgSize = data[4] * 16384;
  const chrSize = data[5] * 8192;
  const expected = prgOffset + prgSize + chrSize;

  if (prgSize === 0) fail('ROM inválida: PRG vazio');
  if (data.length !== expected) {
    fail(`Tamanho do arquivo não corresponde ao cabeçalho (${data.length} != ${expected})`);
  }

  const chrOffset = prgOffset + prgSize;
  const banks = [];
  for (let offset = prgOffset; offset < chrOffset; offset += 8192) {
    banks.push({ fileOffset: offset, size: 8192 });
  }

  const vectors = {
    nmi: data.readUInt16LE(chrOffset - 6),
    reset: data.readUInt16LE(chrOffset - 4),
    irq: data.readUInt16LE(chrOffset - 2),
  };

  return {
    format,
    mapper,
    trainer,
    prgOffset,
    prgSize,
    chrOffset,
    chrSize,
    banks,
    vectors,
    prgSha256: hash(data, prgOffset, prgSize),
    chrSha256: hash(data, chrOffset, chrSize),
  };
}

export function validateSmb3Rom(data) {
  const report = inspectRom(data);
  if (report.prgSize !== SMB3_PAYLOAD.prgSize || report.chrSize !== SMB3_PAYLOAD.chrSize) {
    fail(`ROM não é Super Mario Bros. 3 (USA): PRG/CHR ${report.prgSize}/${report.chrSize}`);
  }
  if (report.prgSha256 !== SMB3_PAYLOAD.prgSha256) {
    fail('ROM não é a revisão suportada: hash do PRG difere');
  }
  if (report.chrSha256 !== SMB3_PAYLOAD.chrSha256) {
    fail('ROM não é a revisão suportada: hash do CHR difere');
  }
  return report;
}

const invokedDirectly = process.argv[1] &&
  import.meta.url === pathToFileURL(resolve(process.argv[1])).href;

if (invokedDirectly) {
  const [romPath, outDir] = process.argv.slice(2);
  if (!romPath) {
    console.error('Uso: node tools/rom.mjs <ROM.nes> [diretório de extração]');
    process.exit(2);
  }
  try {
    const data = readFileSync(romPath);
    const report = validateSmb3Rom(data);
    console.log(`${romPath}: ${report.format}, mapper ${report.mapper}, ${report.prgSize} PRG, ${report.chrSize} CHR`);
    console.log(`PRG SHA-256: ${report.prgSha256}`);
    console.log(`CHR SHA-256: ${report.chrSha256}`);
    console.log(`Vectors: NMI $${report.vectors.nmi.toString(16)}, RESET $${report.vectors.reset.toString(16)}, IRQ $${report.vectors.irq.toString(16)}`);
    if (outDir) {
      mkdirSync(outDir, { recursive: true });
      writeFileSync(join(outDir, 'prg.bin'), data.subarray(report.prgOffset, report.chrOffset));
      writeFileSync(join(outDir, 'chr.bin'), data.subarray(report.chrOffset));
      console.log(`Extracted prg.bin and chr.bin to ${outDir}`);
    }
  } catch (error) {
    console.error(error.message);
    process.exit(1);
  }
}
