#include "absl/strings/match.h"
#include "core/lsp/QueryResponse.h"
#include "main/lsp/lsp.h"

using namespace std;

namespace sorbet::realmain::lsp {

LSPResult LSPLoop::handleTextDocumentReferences(unique_ptr<core::GlobalState> gs, const MessageId &id,
                                                const ReferenceParams &params) {
    auto response = make_unique<ResponseMessage>("2.0", id, LSPMethod::TextDocumentReferences);
    if (!opts.lspFindReferencesEnabled) {
        response->error =
            make_unique<ResponseError>((int)LSPErrorCodes::InvalidRequest,
                                       "The `Find References` LSP feature is experimental and disabled by default.");
        return LSPResult::make(move(gs), move(response));
    }

    ShowOperation op(*this, "References", "Finding all references...");
    prodCategoryCounterInc("lsp.messages.processed", "textDocument.references");

    auto result = setupLSPQueryByLoc(move(gs), params.textDocument->uri, *params.position,
                                     LSPMethod::TextDocumentCompletion, false);
    if (auto run1 = get_if<TypecheckRun>(&result)) {
        gs = move(run1->gs);
        // An explicit null indicates that we don't support this request (or that nothing was at the location).
        // Note: Need to correctly type variant here so it goes into right 'slot' of result variant.
        response->result = variant<JSONNullObject, vector<unique_ptr<Location>>>(JSONNullObject());
        auto &queryResponses = run1->responses;
        if (!queryResponses.empty()) {
            auto resp = move(queryResponses[0]);

            auto &dispatchComponents = resp->getDispatchComponents();
            if (!dispatchComponents.empty() && dispatchComponents[0].method.exists() &&
                dispatchComponents[0].receiver != nullptr && !dispatchComponents[0].receiver->isUntyped()) {
                auto symRef = dispatchComponents[0].method;
                auto run2 = setupLSPQueryBySymbol(move(gs), symRef);
                gs = move(run2.gs);
                response->result = extractLocations(*gs, run2.responses);
            } else if (auto identResp = resp->isIdent()) {
                std::vector<std::shared_ptr<core::File>> files;
                auto run2 = runLSPQuery(
                    move(gs), core::lsp::Query::createVarQuery(identResp->owner, identResp->variable), files, true);
                gs = move(run2.gs);
                response->result = extractLocations(*gs, run2.responses);
            }
        }
    } else if (auto error = get_if<pair<unique_ptr<ResponseError>, unique_ptr<core::GlobalState>>>(&result)) {
        // An error happened while setting up the query.
        response->error = move(error->first);
        gs = move(error->second);
    } else {
        // Should never happen, but satisfy the compiler.
        ENFORCE(false, "Internal error: setupLSPQueryByLoc returned invalid value.");
    }
    return LSPResult::make(move(gs), move(response));
}

} // namespace sorbet::realmain::lsp
