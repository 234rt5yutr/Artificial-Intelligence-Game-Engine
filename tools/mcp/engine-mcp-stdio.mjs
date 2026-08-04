#!/usr/bin/env node
// stdio <-> HTTP bridge for the AIGameEngine MCP server.
//
// The engine hosts MCP as plain JSON-RPC over HTTP POST. MCP clients that speak
// stdio (Claude Code's default server type) cannot talk to that directly, so this
// forwards newline-delimited JSON-RPC from stdin to the engine and writes replies
// back to stdout.
//
//   node tools/mcp/engine-mcp-stdio.mjs [--url http://127.0.0.1:3000/mcp] [--token <bearer>]
//
// Env fallbacks: AIGE_MCP_URL, AIGE_MCP_TOKEN.
//
// No dependencies: uses global fetch (Node 18+).

import { createInterface } from 'node:readline';

function parseArgs(argv) {
  const args = { url: process.env.AIGE_MCP_URL ?? 'http://127.0.0.1:3000/mcp',
                 token: process.env.AIGE_MCP_TOKEN ?? '' };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--url' && argv[i + 1]) args.url = argv[++i];
    else if (argv[i] === '--token' && argv[i + 1]) args.token = argv[++i];
  }
  return args;
}

const { url, token } = parseArgs(process.argv.slice(2));

// stdout is the protocol channel; every diagnostic must go to stderr or it
// corrupts the stream.
const log = (...parts) => process.stderr.write(`[engine-mcp-stdio] ${parts.join(' ')}\n`);

function send(message) {
  process.stdout.write(JSON.stringify(message) + '\n');
}

function errorResponse(id, code, message) {
  return { jsonrpc: '2.0', id: id ?? null, error: { code, message } };
}

async function forward(line) {
  let parsed;
  try {
    parsed = JSON.parse(line);
  } catch {
    send(errorResponse(null, -32700, 'Parse error'));
    return;
  }

  const isNotification = parsed.id === undefined || parsed.id === null;

  let response;
  try {
    const headers = { 'Content-Type': 'application/json' };
    if (token) headers['Authorization'] = `Bearer ${token}`;
    response = await fetch(url, { method: 'POST', headers, body: line });
  } catch (err) {
    // A notification has no id, so there is nothing the client can correlate a
    // failure with - log it instead of emitting an unanswerable response.
    if (isNotification) return log('transport error on notification:', err.message);
    return send(errorResponse(parsed.id, -32603, `Engine unreachable at ${url}: ${err.message}`));
  }

  // 202 is how the engine acknowledges notifications; there is no body to relay.
  if (response.status === 202) return;

  const body = await response.text();
  if (!body) {
    if (!isNotification) send(errorResponse(parsed.id, -32603, `Empty response (HTTP ${response.status})`));
    return;
  }

  if (!response.ok && response.status !== 400) {
    if (!isNotification) send(errorResponse(parsed.id, -32603, `HTTP ${response.status}: ${body.slice(0, 500)}`));
    return;
  }

  try {
    // Relay verbatim rather than re-serializing, so ids and numeric precision
    // survive the hop.
    JSON.parse(body);
    process.stdout.write(body.endsWith('\n') ? body : body + '\n');
  } catch {
    if (!isNotification) send(errorResponse(parsed.id, -32603, `Engine returned non-JSON: ${body.slice(0, 500)}`));
  }
}

// Requests are forwarded in arrival order; MCP clients correlate by id, but
// serializing keeps engine-side ordering predictable for stateful tools.
let chain = Promise.resolve();
const rl = createInterface({ input: process.stdin, crlfDelay: Infinity });

rl.on('line', (line) => {
  const trimmed = line.trim();
  if (!trimmed) return;
  chain = chain.then(() => forward(trimmed)).catch((err) => log('unhandled:', err.message));
});

// Drain in-flight requests before exiting: stdin closing does not mean the
// engine has finished answering what was already sent.
rl.on('close', () => {
  chain.then(() => process.exit(0)).catch(() => process.exit(0));
});

log(`bridging stdio to ${url}${token ? ' (authenticated)' : ''}`);
