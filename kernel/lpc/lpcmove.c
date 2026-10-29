/*++

Module Name:

    lpcmove.c

Abstract:

    LpcpMoveMessage for ARM (the ARM LPC/MIPS/LPCMOVE.S): copy an LPC message
    header and its data, optionally overriding the message type and client id.

Environment:

    Kernel mode only.

--*/

#include "lpcp.h"

VOID
LpcpMoveMessage (
    OUT PPORT_MESSAGE DstMsg,
    IN PPORT_MESSAGE SrcMsg,
    IN PVOID SrcMsgData,
    IN ULONG MsgType OPTIONAL,
    IN PCLIENT_ID ClientId OPTIONAL
    )

/*++

Routine Description:

    This function moves an LPC message block and optionally sets the message
    type and client id to the specified values.

Arguments:

    DstMsg - Supplies a pointer to the destination message.

    SrcMsg - Supplies a pointer to the source message.

    SrcMsgData - Supplies a pointer to the source message data to copy to the
        destination.

    MsgType - If nonzero, then store in the type field of the destination
        message.

    ClientId - If non-NULL, then points to a client id to copy to the
        destination message.

--*/

{
    ULONG Length = ((ULONG)(USHORT)SrcMsg->u1.s1.DataLength + 3) & 0xfffc;
    PULONG Destination = (PULONG)(DstMsg + 1);
    PULONG Source = (PULONG)SrcMsgData;

    DstMsg->u1.Length = SrcMsg->u1.Length;
    DstMsg->u2.ZeroInit = SrcMsg->u2.ZeroInit;
    if (MsgType != 0) {
        DstMsg->u2.s2.Type = (CSHORT)MsgType;
    }

    DstMsg->ClientId = ARGUMENT_PRESENT(ClientId) ? *ClientId : SrcMsg->ClientId;
    DstMsg->MessageId = SrcMsg->MessageId;
    DstMsg->ClientViewSize = SrcMsg->ClientViewSize;
    for (Length /= 4; Length != 0; Length -= 1) {
        *Destination++ = *Source++;
    }
}
