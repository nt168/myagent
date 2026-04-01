/*
 * Minimal inflate shim for CheckPilot.
 * It performs byte copy only (no real zlib decompression).
 */

export function inflateInit(_strm) {
    return 0;
}

export function inflateReset(_strm) {
    return 0;
}

export function inflate(strm, _flush) {
    if (!strm || !strm.input || !strm.output) {
        return 0;
    }

    const remainingIn = strm.input.length - strm.next_in;
    const remainingOut = strm.output.length - strm.next_out;
    const count = Math.min(remainingIn, remainingOut, strm.avail_in, strm.avail_out);

    if (count > 0) {
        strm.output.set(
            strm.input.subarray(strm.next_in, strm.next_in + count),
            strm.next_out
        );
        strm.next_in += count;
        strm.avail_in -= count;
        strm.next_out += count;
        strm.avail_out -= count;
    }

    return 0;
}
