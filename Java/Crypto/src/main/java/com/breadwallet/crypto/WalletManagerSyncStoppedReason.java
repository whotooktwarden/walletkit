package com.breadwallet.crypto;

import android.support.annotation.Nullable;

import com.google.common.base.Optional;

import java.util.Objects;

public final class WalletManagerSyncStoppedReason {

    // create constant values, where possible
    private static final WalletManagerSyncStoppedReason COMPLETE_REASON = new WalletManagerSyncStoppedReason(Type.COMPLETE);
    private static final WalletManagerSyncStoppedReason REQUESTED_REASON = new WalletManagerSyncStoppedReason(Type.REQUESTED);
    private static final WalletManagerSyncStoppedReason UNKNOWN_REASON = new WalletManagerSyncStoppedReason(Type.UNKNOWN);

    public static WalletManagerSyncStoppedReason COMPLETE() {
        return COMPLETE_REASON;
    }

    public static WalletManagerSyncStoppedReason REQUESTED() {
        return REQUESTED_REASON;
    }

    public static WalletManagerSyncStoppedReason UNKNOWN() {
        return UNKNOWN_REASON;
    }

    public static WalletManagerSyncStoppedReason POSIX(int errNum, String errMessage) {
        return new WalletManagerSyncStoppedReason(Type.POSIX, errNum, errMessage);
    }

    public enum Type { COMPLETE, REQUESTED, UNKNOWN, POSIX }

    private final Type type;

    @Nullable
    private final Integer posixErrnum;

    @Nullable
    private final String posixMessage;

    private WalletManagerSyncStoppedReason(Type type) {
        this(type, null, null);
    }

    private WalletManagerSyncStoppedReason(Type type,
                                           @Nullable Integer posixErrnum,
                                           @Nullable String posixMessage) {
        this.type = type;
        this.posixErrnum = posixErrnum;
        this.posixMessage = posixMessage;
    }

    public Type getType() {
        return type;
    }

    public Optional<Integer> getPosixErrnum() {
        return Optional.fromNullable(posixErrnum);
    }

    public Optional<String> getPosixMessage() {
        return Optional.fromNullable(posixMessage);
    }

    @Override
    public String toString() {
        switch (type) {
            case COMPLETE:
                return "Complete";
            case REQUESTED:
                return "Requested";
            case UNKNOWN:
                return "Unknown";
            case POSIX:
                return String.format("Posix (%d: %s)", posixErrnum, posixMessage);
            default:
                return super.toString();
        }
    }

    @Override
    public boolean equals(Object object) {
        if (this == object) {
            return true;
        }

        if (!(object instanceof WalletManagerSyncStoppedReason)) {
            return false;
        }

        WalletManagerSyncStoppedReason that = (WalletManagerSyncStoppedReason) object;
        return type == that.type &&
                Objects.equals(posixErrnum, that.posixErrnum);
    }

    @Override
    public int hashCode() {
        return Objects.hash(type, posixErrnum);
    }
}
