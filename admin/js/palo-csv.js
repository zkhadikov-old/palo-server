/**
 * Palo HTTP API répond en text/plain : champs séparés par ';', chaînes entre guillemets, "" pour échapper ".
 */
(function (global) {
  function parsePaloCsvLine(line) {
    const out = [];
    let i = 0;
    const len = line.length;
    while (i < len) {
      if (line[i] === '"') {
        i += 1;
        let s = '';
        while (i < len) {
          if (line[i] === '"' && line[i + 1] === '"') {
            s += '"';
            i += 2;
            continue;
          }
          if (line[i] === '"') {
            i += 1;
            break;
          }
          s += line[i];
          i += 1;
        }
        out.push(s);
        if (i < len && line[i] === ';') i += 1;
        continue;
      }
      const start = i;
      while (i < len && line[i] !== ';') i += 1;
      out.push(line.slice(start, i));
      if (i < len && line[i] === ';') i += 1;
    }
    return out;
  }

  function parsePaloBody(text) {
    const lines = String(text).split(/\r?\n/).filter(function (l) {
      return l.length > 0;
    });
    return lines.map(parsePaloCsvLine);
  }

  function parsePaloError(text) {
    const lines = parsePaloBody(text);
    if (!lines.length) return { code: -1, message: text || 'Erreur inconnue' };
    const f = lines[0];
    const code = parseInt(f[0], 10);
    const message = f[2] || f[1] || text;
    return { code: isNaN(code) ? -1 : code, message: message };
  }

  global.PaloCsv = {
    parseLine: parsePaloCsvLine,
    parseBody: parsePaloBody,
    parseError: parsePaloError,
  };
})(typeof window !== 'undefined' ? window : globalThis);
