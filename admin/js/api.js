/**
 * Client HTTP minimal pour l’API Palo (GET, sid).
 *
 * Politique X-PALO-* : on ne renvoie pas les en-têtes X-PALO-SV / DB / DIM / CB / CC sur les requêtes.
 * Les réponses les mettent à jour (mergeHeaders), mais après une écriture le jeton base/dim/cube côté
 * serveur change souvent sans être renvoyé dans la réponse → envoyer l’ancien jeton provoque 400
 * « server/database/dimension/cube token outdated ». Le serveur accepte l’absence de jeton
 * (contrôle d’optimistic locking désactivé, voir PaloJob::checkToken).
 */
(function (global) {
  function md5PasswordHex(password) {
    return CryptoJS.MD5(CryptoJS.enc.Utf8.parse(password)).toString(CryptoJS.enc.Hex);
  }

  function mergeHeaders(headers, tokens) {
    const map = [
      ['x-palo-sv', 'sv'],
      ['x-palo-db', 'db'],
      ['x-palo-dim', 'dim'],
      ['x-palo-cb', 'cube'],
      ['x-palo-cc', 'cc'],
    ];
    for (let i = 0; i < map.length; i++) {
      const v = headers.get(map[i][0]);
      if (v != null && v !== '') tokens[map[i][1]] = v;
    }
  }

  function buildRequestHeaders(tokens) {
    const h = {};
    if (tokens.sv) h['X-PALO-SV'] = tokens.sv;
    if (tokens.db) h['X-PALO-DB'] = tokens.db;
    if (tokens.dim) h['X-PALO-DIM'] = tokens.dim;
    if (tokens.cube) h['X-PALO-CB'] = tokens.cube;
    if (tokens.cc) h['X-PALO-CC'] = tokens.cc;
    return h;
  }

  /** Options fetch communes (CORS explicite — l’UI est souvent sur un autre port que l’API). */
  var fetchCorsOpts = { mode: 'cors', credentials: 'omit' };

  function wrapFetchCorsError(err) {
    var msg = (err && err.message) ? String(err.message) : String(err);
    if (
      msg.indexOf('Failed to fetch') !== -1 ||
      msg.indexOf('NetworkError') !== -1 ||
      msg.indexOf('Load failed') !== -1
    ) {
      return new Error(
        msg +
          ' — Si l’interface n’est pas sur la même origine que Palo, ajoutez « cross-origin * » (ou votre origine) dans palo.ini, ou lancez palo avec -g \'*\', puis redémarrez le serveur.'
      );
    }
    return err instanceof Error ? err : new Error(msg);
  }

  function PaloClient(baseUrl) {
    this.baseUrl = baseUrl.replace(/\/$/, '');
    this.sid = null;
    this.tokens = { sv: null, db: null, dim: null, cube: null, cc: null };
  }

  PaloClient.prototype._url = function (path, params) {
    const u = new URL(path.replace(/^\//, ''), this.baseUrl + '/');
    const p = params || {};
    if (this.sid) p.sid = this.sid;
    const sp = new URLSearchParams();
    Object.keys(p).forEach(function (k) {
      if (p[k] != null && p[k] !== '') sp.set(k, String(p[k]));
    });
    u.search = sp.toString();
    return u.toString();
  };

  /** Réinitialise les jetons Palo avant une requête — voir commentaire de module (évite 400 token outdated). */
  PaloClient.prototype._resetPaloTokensForNextRequest = function () {
    this.tokens.sv = null;
    this.tokens.db = null;
    this.tokens.dim = null;
    this.tokens.cube = null;
    this.tokens.cc = null;
  };

  PaloClient.prototype._fetch = function (path, params) {
    const self = this;
    this._resetPaloTokensForNextRequest();
    const headers = buildRequestHeaders(this.tokens);
    return fetch(this._url(path, params), Object.assign({ headers: headers }, fetchCorsOpts))
      .catch(function (e) {
        throw wrapFetchCorsError(e);
      })
      .then(function (r) {
        mergeHeaders(r.headers, self.tokens);
        return r.text().then(function (text) {
          if (!r.ok) {
            const err = PaloCsv.parseError(text);
            const e = new Error(err.message);
            e.paloCode = err.code;
            e.body = text;
            throw e;
          }
          return text;
        });
      });
  };

  PaloClient.prototype.login = function (user, password) {
    const self = this;
    const params = {
      user: user,
      password: md5PasswordHex(password),
    };
    this.sid = null;
    this.tokens = { sv: null, db: null, dim: null, cube: null, cc: null };
    const headers = buildRequestHeaders(this.tokens);
    return fetch(this._url('/server/login', params), Object.assign({ headers: headers }, fetchCorsOpts))
      .catch(function (e) {
        throw wrapFetchCorsError(e);
      })
      .then(function (r) {
        mergeHeaders(r.headers, self.tokens);
        return r.text().then(function (text) {
          if (!r.ok) {
            const err = PaloCsv.parseError(text);
            const e = new Error(err.message);
            e.paloCode = err.code;
            throw e;
          }
          const rows = PaloCsv.parseBody(text);
          if (!rows.length) throw new Error('Réponse login vide');
          self.sid = rows[0][0];
          return { sid: self.sid, ttl: rows[0][1] };
        });
      });
  };

  /** Infos session : groupes dont « admin » => droits étendus (cubes / dimensions système, etc.). */
  PaloClient.prototype.userInfo = function () {
    return this._fetch('/server/user_info', {}).then(function (text) {
      const rows = PaloCsv.parseBody(text);
      if (!rows.length) {
        return { isAdmin: false, groupNames: [] };
      }
      const f = rows[0];
      const groupNamesStr = f[3] != null ? String(f[3]) : '';
      const groupNames = groupNamesStr
        .split(',')
        .map(function (s) {
          return s.trim();
        })
        .filter(Boolean);
      const isAdmin = groupNames.indexOf('admin') !== -1;
      return {
        id: f[0],
        name: f[1],
        groupNames: groupNames,
        isAdmin: isAdmin,
      };
    });
  };

  /**
   * Mot de passe en clair (comme documenté pour /server/change_password), pas le MD5 du login.
   * @param {string} [targetUsername] — utilisateur à modifier ; omis = mot de passe de la session courante
   */
  PaloClient.prototype.changePassword = function (targetUsername, newPasswordPlain) {
    const params = { password: newPasswordPlain };
    if (targetUsername != null && String(targetUsername).trim() !== '') {
      params.user = String(targetUsername).trim();
    }
    return this._fetch('/server/change_password', params);
  };

  /** Écrit une cellule (ex. rattachement utilisateur ↔ groupe). path = ids d’éléments séparés par des virgules. */
  PaloClient.prototype.replaceCell = function (databaseId, cubeId, pathCommaIds, value, extraParams) {
    const params = {
      database: databaseId,
      cube: cubeId,
      path: pathCommaIds,
      value: value,
    };
    if (extraParams && typeof extraParams === 'object') {
      Object.keys(extraParams).forEach(function (k) {
        if (extraParams[k] != null) params[k] = extraParams[k];
      });
    }
    return this._fetch('/cell/replace', params);
  };

  PaloClient.prototype.logout = function () {
    const self = this;
    if (!this.sid) return Promise.resolve();
    return this._fetch('/server/logout', {}).then(function () {
      self.sid = null;
      self.tokens = { sv: null, db: null, dim: null, cube: null, cc: null };
    }).catch(function () {
      self.sid = null;
      self.tokens = { sv: null, db: null, dim: null, cube: null, cc: null };
    });
  };

  PaloClient.prototype.listDatabases = function (extraParams) {
    const p = {};
    if (extraParams && typeof extraParams === 'object') {
      Object.keys(extraParams).forEach(function (k) {
        if (extraParams[k] != null) p[k] = extraParams[k];
      });
    }
    return this._fetch('/server/databases', p).then(function (text) {
      return PaloCsv.parseBody(text)
        .filter(function (f) {
          return f.length >= 2;
        })
        .map(parseDatabaseRow);
    });
  };

  function parseDatabaseRow(fields) {
    return {
      database: fields[0],
      name_database: fields[1],
      number_dimensions: fields[2],
      number_cubes: fields[3],
      status: fields[4],
      type: fields[5],
      database_token: fields[6],
    };
  }

  PaloClient.prototype.createDatabase = function (newName) {
    return this._fetch('/database/create', { new_name: newName }).then(function (text) {
      const row = PaloCsv.parseBody(text)[0];
      return parseDatabaseRow(row);
    });
  };

  /**
   * @param {string|number} databaseId
   * @param {Object} [extraParams] ex. { show_system: '1', show_attribute: '1', show_info: '1' } pour les comptes admin
   */
  PaloClient.prototype.listDimensions = function (databaseId, extraParams) {
    const p = { database: databaseId };
    if (extraParams && typeof extraParams === 'object') {
      Object.keys(extraParams).forEach(function (k) {
        if (extraParams[k] != null) p[k] = extraParams[k];
      });
    }
    return this._fetch('/database/dimensions', p).then(function (text) {
      return PaloCsv.parseBody(text).map(parseDimensionRow);
    });
  };

  function parseDimensionRow(fields) {
    return {
      dimension: fields[0],
      name_dimension: fields[1],
      number_elements: fields[2],
      maximum_level: fields[3],
      maximum_indent: fields[4],
      maximum_depth: fields[5],
      type: fields[6],
      dimension_token: fields[fields.length - 1],
    };
  }

  PaloClient.prototype.createDimension = function (databaseId, newName) {
    return this._fetch('/dimension/create', { database: databaseId, new_name: newName }).then(function (text) {
      return parseDimensionRow(PaloCsv.parseBody(text)[0]);
    });
  };

  /**
   * @param {Object} [extraParams] ex. flags show_system / show_attribute / show_info pour lister les cubes « administration »
   */
  PaloClient.prototype.listCubes = function (databaseId, extraParams) {
    const p = { database: databaseId };
    if (extraParams && typeof extraParams === 'object') {
      Object.keys(extraParams).forEach(function (k) {
        if (extraParams[k] != null) p[k] = extraParams[k];
      });
    }
    return this._fetch('/database/cubes', p).then(function (text) {
      return PaloCsv.parseBody(text).map(parseCubeRow);
    });
  };

  function parseCubeRow(fields) {
    return {
      cube: fields[0],
      name_cube: fields[1],
      number_dimensions: fields[2],
      dimensions: fields[3],
      number_cells: fields[4],
      number_filled_cells: fields[5],
      status: fields[6],
      type: fields[7],
      cube_token: fields[fields.length - 1],
    };
  }

  PaloClient.prototype.createCube = function (databaseId, newName, dimensionIdsComma) {
    return this._fetch('/cube/create', {
      database: databaseId,
      new_name: newName,
      dimensions: dimensionIdsComma,
    }).then(function (text) {
      return parseCubeRow(PaloCsv.parseBody(text)[0]);
    });
  };

  PaloClient.prototype.destroyCube = function (databaseId, cubeId) {
    return this._fetch('/cube/destroy', { database: databaseId, cube: cubeId });
  };

  PaloClient.prototype.destroyDimension = function (databaseId, dimensionId) {
    return this._fetch('/dimension/destroy', { database: databaseId, dimension: dimensionId });
  };

  PaloClient.prototype.listElements = function (databaseId, dimensionId) {
    return this._fetch('/dimension/elements', { database: databaseId, dimension: dimensionId }).then(function (text) {
      return PaloCsv.parseBody(text).map(parseElementRow);
    });
  };

  function parseElementRow(fields) {
    return {
      element: fields[0],
      name_element: fields[1],
      position: fields[2],
      level: fields[3],
      indent: fields[4],
      depth: fields[5],
      type: fields[6],
      number_parents: fields[7],
      parents: fields[8] || '',
      number_children: fields[9],
      children: fields[10] || '',
      weights: fields[11] || '',
    };
  }

  PaloClient.prototype.createElement = function (databaseId, dimensionId, newName, typeNum, childrenCsv, weightsCsv) {
    const params = {
      database: databaseId,
      dimension: dimensionId,
      new_name: newName,
      type: String(typeNum),
    };
    if (typeNum === 4 && childrenCsv) params.children = childrenCsv;
    if (weightsCsv) params.weights = weightsCsv;
    return this._fetch('/element/create', params).then(function (text) {
      return parseElementRow(PaloCsv.parseBody(text)[0]);
    });
  };

  /**
   * Ajoute des enfants à un élément existant ; le type passe à consolidé si besoin (API /element/append).
   */
  PaloClient.prototype.elementAppend = function (databaseId, dimensionId, parentElementId, childrenCsv, weightsCsv) {
    const params = {
      database: databaseId,
      dimension: dimensionId,
      element: parentElementId,
      children: childrenCsv,
    };
    if (weightsCsv) params.weights = weightsCsv;
    return this._fetch('/element/append', params).then(function (text) {
      return parseElementRow(PaloCsv.parseBody(text)[0]);
    });
  };

  PaloClient.prototype.destroyElement = function (databaseId, dimensionId, elementId) {
    return this._fetch('/element/destroy', {
      database: databaseId,
      dimension: dimensionId,
      element: elementId,
    });
  };

  PaloClient.prototype.listRules = function (databaseId, cubeId) {
    return this._fetch('/cube/rules', { database: databaseId, cube: cubeId }).then(function (text) {
      const body = String(text).trim();
      if (!body) return [];
      return PaloCsv.parseBody(text).map(parseRuleRow);
    });
  };

  function parseRuleRow(fields) {
    return {
      rule: fields[0],
      rule_string: fields[1],
      external_identifier: fields[2],
      comment: fields[3],
      timestamp: fields[4],
      active: fields[5],
      position: fields[6],
    };
  }

  PaloClient.prototype.createRule = function (databaseId, cubeId, definition) {
    return this._fetch('/rule/create', {
      database: databaseId,
      cube: cubeId,
      definition: definition,
      use_identifier: '0',
    }).then(function (text) {
      return parseRuleRow(PaloCsv.parseBody(text)[0]);
    });
  };

  PaloClient.prototype.destroyRule = function (databaseId, cubeId, ruleId) {
    return this._fetch('/rule/destroy', {
      database: databaseId,
      cube: cubeId,
      rule: String(ruleId),
    });
  };

  /** Optionnel : _fetch réinitialise déjà tous les jetons ; utile si l’UI veut forger un état « sans contexte ». */
  PaloClient.prototype.clearDbToken = function () {
    this.tokens.db = null;
    this.tokens.dim = null;
    this.tokens.cube = null;
    this.tokens.cc = null;
  };

  PaloClient.prototype.clearDimToken = function () {
    this.tokens.dim = null;
  };

  PaloClient.prototype.clearCubeToken = function () {
    this.tokens.cube = null;
    this.tokens.cc = null;
  };

  global.PaloClient = PaloClient;
  global.paloMd5PasswordHex = md5PasswordHex;
})(typeof window !== 'undefined' ? window : globalThis);
